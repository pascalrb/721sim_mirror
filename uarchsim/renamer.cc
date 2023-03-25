#include "renamer.h"

#include <assert.h>     
#include <stdio.h>  //p_rintf
#include <iostream> //c_out

//using namespace std;

//constructor
renamer::renamer(uint64_t n_log_regs,
		         uint64_t n_phys_regs,
		         uint64_t n_branches,
		         uint64_t n_active)
{
    //printf("renamer constructor\n");
    //printf("    log_regs: %lu, phys_regs: %lu, AL_Size: %lu\n", n_log_regs, n_phys_regs, n_active);
    debug_cnt = 0;

    assert(n_phys_regs > n_log_regs);
    assert(1 <= n_branches && n_branches <= MAX_UNRESOLVED_BRANCHES);
    assert(n_active > 0);

    // initializing private vars  
    LOGREG_RMT_AMT_SIZE         = n_log_regs;
    PHYS_REG_SIZE               = n_phys_regs;
    UNRESOLVED_BRANCHES_SIZE    = n_branches;
    AL_SIZE                     = n_active;
    FL_SIZE                     = n_phys_regs - n_log_regs;

    // PRF init. Just allocate space for it
    PRF.resize(PHYS_REG_SIZE);
    for(uint64_t i=0; i<PHYS_REG_SIZE; i++){
        //initially all of the PRF_ready_bits are available to be used
        // (in AMT & FL)
        PRF_rb.push_back(true);
    }

    // AMT & RMT init. initially AMT & RMT have same value
    for(uint64_t i=0; i<LOGREG_RMT_AMT_SIZE; i++){
        //initially AMT & RMT point to the first [n_log_regs] regs of the PRF
        RMT.push_back(i);   
        AMT.push_back(i);   
    }
    
    // Free List init
    FL.head = 0;
    FL.tail = 0;
    FL.head_pb = false;     // 0 ]___\ full
    FL.tail_pb = true;      // 1 ]   /
    // intially the remainder of Phys Regs are free (n_phys_regs (allocated in RMT&AMT) - n_log_regs)
    for(uint64_t i=LOGREG_RMT_AMT_SIZE; i<PHYS_REG_SIZE; i++){
        FL.fl_regs.push_back(i); 
    }

    // Active List init
    AL.head = 0;
    AL.tail = 0;
    AL.head_pb = false;     // 0 ]___\ empty
    AL.tail_pb = false;     // 0 ]   /
    AL.AL_entries.resize(AL_SIZE);

    // Allocate space for the Branch Checkpoints
    BCs.resize(UNRESOLVED_BRANCHES_SIZE);

    GBM = 0;

    //printf("    AMT: %lu, RMT: %lu, AL: %lu, FL: %lu, BCs: %lu\n", AMT.size(), RMT.size(), AL.AL_entries.size(), FL.fl_regs.size(), BCs.size());
}

//destructor
renamer::~renamer()
{
    //printf("renamer destructor\n");

}


bool renamer::stall_reg(uint64_t bundle_dst)
{
    //printf("stall_reg()\n");

    if(FL.head == FL.tail && FL.head_pb == FL.tail_pb){  //empty
        return true;
    }else{
        if(FL.head_pb == FL.tail_pb){
           if((FL.tail - FL.head) >= bundle_dst){
               return false;
           }else{
               return true;
           }
        }else{
           if((FL_SIZE - (FL.head - FL.tail)) >= bundle_dst){
               return false;
           }else{
               return true;
           }
        }
    }
}

bool renamer::stall_branch(uint64_t bundle_branch)
{
    //printf("stall_branch()\n");

    uint64_t BCs_unset_count = 0;

    for(uint64_t i=0; i<UNRESOLVED_BRANCHES_SIZE; i++){
        if((GBM & (1ULL<<i)) == 0){
            BCs_unset_count++;
        }
    }

    if(BCs_unset_count >= bundle_branch){
        return false;
    }else{
        return true;
    }

}

uint64_t renamer::get_branch_mask()
{
    //printf("get_branch_mask() - %016lx\n", GBM);

    return GBM;
}

uint64_t renamer::rename_rsrc(uint64_t log_reg)
{
    //printf("rename_rsrc()\n");

    return RMT[log_reg];
}

uint64_t renamer::rename_rdst(uint64_t log_reg)
{
    //printf("rename_rdst()\n");

    uint64_t phys_reg = FL.fl_regs[FL.head];

    //update RMT
    RMT[log_reg] = phys_reg;

    //update FL 
    if (FL.head == FL_SIZE-1){
        FL.head = 0;
        FL.head_pb = !FL.head_pb;
    }else{
        FL.head++;
    }

    return phys_reg;
}

uint64_t renamer::checkpoint()
{
    //printf("checkpoint()\n");

    int64_t branch_id = -1;

    //find a free bit in GBM
    for(uint64_t i=0; i<UNRESOLVED_BRANCHES_SIZE; i++){
        if((GBM & (1ULL<<i)) == 0){
            branch_id = i;
            break;
        }
    }
    assert(branch_id != -1);

    //setting bit in GBM
    GBM |= (1ULL<<branch_id);

    BCs[branch_id].RMT_copy = RMT;
    BCs[branch_id].fl_head_copy = FL.head;
    BCs[branch_id].fl_head_pb_copy = FL.head_pb;
    BCs[branch_id].GBM_copy = GBM;
    
    return branch_id;
}

bool renamer::stall_dispatch(uint64_t bundle_inst)
{
    //printf("stall_dispatch()\n");

    if(AL.head == AL.tail && AL.head_pb != AL.tail_pb){  //full
        return true;
    }else{
        if(AL.head_pb == AL.tail_pb){
           if((AL_SIZE - (AL.tail - AL.head)) >= bundle_inst){
               return false;
           }else{
               return true;
           }
        }else{
           if((AL.head - AL.tail) >= bundle_inst){
               return false;
           }else{
               return true;
           }
        }
    }
}

uint64_t renamer::dispatch_inst(bool dest_valid,
                                uint64_t log_reg,
                                uint64_t phys_reg,
                                bool load,
                                bool store,
                                bool branch,
                                bool amo,
                                bool csr,
                                uint64_t PC)
{
    //printf("dispatch_inst()\n");

    uint64_t tail_cp = AL.tail;

    //update AL
    AL.AL_entries[AL.tail].has_dest_reg = dest_valid;
    if(dest_valid){
        AL.AL_entries[AL.tail].dest_log_reg = log_reg;
        AL.AL_entries[AL.tail].dest_phys_reg = phys_reg;
    }
    AL.AL_entries[AL.tail].is_completed             = false;
    AL.AL_entries[AL.tail].is_exception             = false;
    AL.AL_entries[AL.tail].is_load_violated         = false;
    AL.AL_entries[AL.tail].is_branch_mispredicted   = false;
    AL.AL_entries[AL.tail].is_value_mispredicted    = false;
    AL.AL_entries[AL.tail].is_load_instr            = load;
    AL.AL_entries[AL.tail].is_store_instr           = store;
    AL.AL_entries[AL.tail].is_branch_instr          = branch;
    AL.AL_entries[AL.tail].is_amo_instr             = amo;
    AL.AL_entries[AL.tail].is_csr_instr             = csr;
    AL.AL_entries[AL.tail].PC                       = PC;

    if (AL.tail == AL_SIZE-1){
        AL.tail = 0;
        AL.tail_pb = !AL.tail_pb;
    }else{
        AL.tail++;
    }

    return tail_cp;
}

bool renamer::is_ready(uint64_t phys_reg)
{
    //printf("is_ready() - phys_reg: %lu\n", phys_reg);

    return PRF_rb[phys_reg];
}

void renamer::clear_ready(uint64_t phys_reg)
{
    //printf("clear_ready() - phys_reg: %lu\n", phys_reg);

    PRF_rb[phys_reg] = false;
}


uint64_t renamer::read(uint64_t phys_reg)
{
    //printf("read() - phys_reg: %lu\n", phys_reg);

    return PRF[phys_reg];
}

void renamer::set_ready(uint64_t phys_reg)
{
    //printf("set_ready() - phys_reg: %lu\n", phys_reg);

    PRF_rb[phys_reg] = true;
}

void renamer::write(uint64_t phys_reg, uint64_t value)
{
    //printf("write() - phys_reg: %lu, value: %lu\n", phys_reg, value);

    PRF[phys_reg] = value;
}

void renamer::set_complete(uint64_t AL_index)
{
    //printf("set_complete()\n");

    AL.AL_entries[AL_index].is_completed = true;
}

void renamer::resolve(uint64_t AL_index,
	                  uint64_t branch_ID,
	                  bool correct)
{
    //printf("resolve() - branch_ID: %lu\n", branch_ID);

    if(correct){
        //clear branch IDth bit in GBM
        GBM &= ~(1ULL<<branch_ID);

        //clear branch IDth bit in all checkpointed GBMs
        for(uint64_t i=0; i<UNRESOLVED_BRANCHES_SIZE; i++){
            BCs[i].GBM_copy &= ~(1ULL<<branch_ID);
        }

    }else{
        //restore FL
        FL.head = BCs[branch_ID].fl_head_copy;
        FL.head_pb = BCs[branch_ID].fl_head_pb_copy;

        //Restoring AL tail to the entry just after the mispredicted branch 
        if(AL_index == AL_SIZE-1){
            AL.tail = 0;
        }else{
            AL.tail = AL_index+1;
        }
        //restoring AL tail phase bit
        if(AL.tail_pb == AL.head_pb){
            if(AL_index == AL_SIZE-1){
                AL.tail_pb = !AL.tail_pb;
            }
        }else{
            if(AL_index >= AL.head){
                if(AL_index != AL_SIZE-1){
                    AL.tail_pb = !AL.tail_pb;
                }
            }
        }


        //clear branch IDth bit in GBM_copy of IDth BC (freeing the BC)
        BCs[branch_ID].GBM_copy &= ~(1ULL<<branch_ID);

        //restoring structures from the IDth branch
        GBM = BCs[branch_ID].GBM_copy;
        RMT = BCs[branch_ID].RMT_copy;

    }
}

bool renamer::precommit(bool &completed,
                        bool &exception, bool &load_viol, bool &br_misp, bool &val_misp,
                        bool &load, bool &store, bool &branch, bool &amo, bool &csr,
	                    uint64_t &PC)
{
    //printf("precommit(): \n");
    //printf("    AL.head: %lu,%-10d FL.head: %lu,%d\n", AL.head, AL.head_pb, FL.head, FL.head_pb);
    //printf("    AL.tail: %lu,%-10d FL.tail: %lu,%d\n", AL.tail, AL.tail_pb, FL.tail, FL.tail_pb);
    //cout <<"    AL.head: "<< AL.head<<", "<< AL.head_pb<<"    FL.head: "<< FL.head<<", "<< FL.head_pb<<endl;
    //cout <<"    AL.tail: "<< AL.tail<<", "<< AL.tail_pb<<"    FL.tail: "<< FL.tail<<", "<< FL.tail_pb<<endl;
    //debug_cnt++;
    //if(debug_cnt == 1000){
    //    assert(0 == 1);
    //}

    if(AL.head == AL.tail && AL.head_pb == AL.tail_pb){
        //AL is empty
        //printf("    %d\n", false);
        return false;
    }else{
        //return instr at head of AL
        completed   = AL.AL_entries[AL.head].is_completed;
        exception   = AL.AL_entries[AL.head].is_exception; 
        load_viol   = AL.AL_entries[AL.head].is_load_violated;
        br_misp     = AL.AL_entries[AL.head].is_branch_mispredicted;
        val_misp    = AL.AL_entries[AL.head].is_value_mispredicted;
        load        = AL.AL_entries[AL.head].is_load_instr;
        store       = AL.AL_entries[AL.head].is_store_instr;
        branch      = AL.AL_entries[AL.head].is_branch_instr;
        amo         = AL.AL_entries[AL.head].is_amo_instr;
        csr         = AL.AL_entries[AL.head].is_csr_instr;
        PC          = AL.AL_entries[AL.head].PC;

        //printf("    %d\n", true);
        return true;
    }

}

void renamer::commit()
{
    //printf("commit()\n");

    assert( !(AL.head == AL.tail && AL.head_pb == AL.tail_pb) );
    assert( AL.AL_entries[AL.head].is_completed );
    assert( !(AL.AL_entries[AL.head].is_exception) );
    assert( !(AL.AL_entries[AL.head].is_load_violated) );

    if(AL.AL_entries[AL.head].has_dest_reg){
        //pushing into FL (freeing previous AMT phys reg mapping)
        FL.fl_regs[FL.tail] = AMT[AL.AL_entries[AL.head].dest_log_reg];
        if (FL.tail == FL_SIZE-1){
            FL.tail = 0;
            FL.tail_pb = !FL.tail_pb;
        }else{
            FL.tail++;
        }

        //commit phys reg into the AMT
        AMT[AL.AL_entries[AL.head].dest_log_reg] = AL.AL_entries[AL.head].dest_phys_reg;
    }

    //increment AL.head 
    if (AL.head == AL_SIZE-1){
        AL.head = 0;
        AL.head_pb = !AL.head_pb;
    }else{
        AL.head++;
    }

}

void renamer::squash()
{
    //printf("squash()\n");

    //restoring RMT
    RMT = AMT;

    //restoring FL to full
    FL.head = FL.tail;
    FL.head_pb = !FL.tail_pb;

    //restoring AL to empty
    AL.tail = AL.head;
    AL.tail_pb = AL.head_pb;


    for(uint64_t i=0; i<PHYS_REG_SIZE; i++){
        //update PRF ready bit to indicate that phys_reg is ready to be picked up
        PRF_rb[i] = true;
    }


    GBM = 0;
    for(uint64_t i=0; i<UNRESOLVED_BRANCHES_SIZE; i++){
        BCs[i].GBM_copy = 0;
    }
}

void renamer::set_exception(uint64_t AL_index)
{
    //printf("set_exception()\n");

    AL.AL_entries[AL_index].is_exception = true;
}

void renamer::set_load_violation(uint64_t AL_index)
{
    //printf("set_load_violation()\n");

    AL.AL_entries[AL_index].is_load_violated = true;
}

void renamer::set_branch_misprediction(uint64_t AL_index)
{
    //printf("set_branch_misprediction()\n");

    AL.AL_entries[AL_index].is_branch_mispredicted = true;
}

void renamer::set_value_misprediction(uint64_t AL_index)
{
    //printf("set_value_misprediction()\n");

    AL.AL_entries[AL_index].is_value_mispredicted = true;
}

bool renamer::get_exception(uint64_t AL_index)
{
    //printf("get_exception()\n");

    return AL.AL_entries[AL_index].is_exception;
}
