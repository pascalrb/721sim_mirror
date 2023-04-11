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
    //AL_SIZE                     = n_active;       //TODO: may no longer need
    FL_SIZE                     = n_phys_regs - n_log_regs;

    // PRF init. Just allocate space for it
    PRF.resize(PHYS_REG_SIZE);
    for(uint64_t i=0; i<PHYS_REG_SIZE; i++){
        //initially all of the PRF_ready_bits are available to be used
        // (in FL)
        PRF_rb.push_back(true);

        //initially all Phys Regs are unmapped 
        PRFUnnmappedBits.push_back(true);
        PRFUsageCounter.push_back(0);
    }

    // RMT init. initially RMT have same value
    for(uint64_t i=0; i<LOGREG_RMT_AMT_SIZE; i++){
        //initially RMT point to the first [n_log_regs] regs of the PRF
        RMT.push_back(i);   

        //Phys Regs mapped to RMT need their unmapped bit set to false
        PRFUnnmappedBits[i] = false;
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

    // Initialize first "oldest" checkpoint buffer
    CPBuffer.head = 0;                                   
    CPBuffer.tail = 0;
    CPBuffer.head_pb = false;     // 0 ]___\ empty
    CPBuffer.tail_pb = false;     // 0 ]   /
    CPBuffer.CPBuffEntries.resize(n_branches);

    CPBuffer.CPBuffEntries[CPBuffer.tail].RMT_copy                  = RMT;
    CPBuffer.CPBuffEntries[CPBuffer.tail].PRFUnnmappedBits_copy     = PRFUnnmappedBits;
    CPBuffer.CPBuffEntries[CPBuffer.tail].PRFUsageCounter_copy      = PRFUsageCounter;
    CPBuffer.CPBuffEntries[CPBuffer.tail].uncompleted_instr_count   = 0;
    CPBuffer.CPBuffEntries[CPBuffer.tail].load_count                = 0;
    CPBuffer.CPBuffEntries[CPBuffer.tail].store_count               = 0;
    CPBuffer.CPBuffEntries[CPBuffer.tail].branch_count              = 0;
    CPBuffer.CPBuffEntries[CPBuffer.tail].has_amo_instr             = false;
    CPBuffer.CPBuffEntries[CPBuffer.tail].has_csr_instr             = false;
    CPBuffer.CPBuffEntries[CPBuffer.tail].has_except_instr          = false;
    CPBuffer.tail++; 


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


uint64_t renamer::rename_rsrc(uint64_t log_reg)
{
    //printf("rename_rsrc()\n");

    uint64_t phys_reg = RMT[log_reg];

    //Update usage counter corresponding to the phys reg
    PRFUsageCounter[phys_reg]++; 

    return phys_reg;
}

uint64_t renamer::rename_rdst(uint64_t log_reg)
{
    //printf("rename_rdst()\n");

    uint64_t phys_reg = FL.fl_regs[FL.head];

    //Update usage counter corresponding to the phys reg
    PRFUnnmappedBits[phys_reg] = false;
    PRFUsageCounter[phys_reg]++; 

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

bool renamer::stall_checkpoint(uint64_t bundle_chkpts)
{
    if(CPBuffer.head == CPBuffer.tail && CPBuffer.head_pb != CPBuffer.tail_pb){  //full
        return true;
    }else{
        if(CPBuffer.head_pb == CPBuffer.tail_pb){
           if((UNRESOLVED_BRANCHES_SIZE - (CPBuffer.tail - CPBuffer.head)) >= bundle_chkpts){
               return false;
           }else{
               return true;
           }
        }else{
           if((CPBuffer.head - CPBuffer.tail) >= bundle_chkpts){
               return false;
           }else{
               return true;
           }
        }
    }
}


//TODO: no longer need
//bool renamer::stall_branch(uint64_t bundle_branch)
//{
//    //printf("stall_branch()\n");
//
//    uint64_t BCs_unset_count = 0;
//
//    for(uint64_t i=0; i<UNRESOLVED_BRANCHES_SIZE; i++){
//        if((GBM & (1ULL<<i)) == 0){
//            BCs_unset_count++;
//        }
//    }
//
//    if(BCs_unset_count >= bundle_branch){
//        return false;
//    }else{
//        return true;
//    }
//}

//TODO: need modified or no longer need
//uint64_t renamer::get_branch_mask()  
//{
//    //printf("get_branch_mask() - %016lx\n", GBM);
//
//    return GBM;
//}

uint64_t renamer::get_checkpoint_ID(bool load, bool store, 
                                    bool branch, bool amo, 
                                    bool csr)  
{
    //printf("get_checkpoint_ID() - %016lx\n", GBM);

    //need prior checkpoint because CPBuff.tail points to next free entry
    uint64_t prior_chkpt;

    if (CPBuffer.tail == 0){
        prior_chkpt = UNRESOLVED_BRANCHES_SIZE - 1;
    }else{
        prior_chkpt = CPBuffer.tail - 1;
    }

    if(load){
        CPBuffer.CPBuffEntries[prior_chkpt].load_count++;
    }else if(store){
        CPBuffer.CPBuffEntries[prior_chkpt].store_count++;
    }else if(branch){
        CPBuffer.CPBuffEntries[prior_chkpt].branch_count++;
    }

    if(amo){
        CPBuffer.CPBuffEntries[prior_chkpt].has_amo_instr = true;
    }
    if(csr){
        CPBuffer.CPBuffEntries[prior_chkpt].has_csr_instr = true;
    }

    CPBuffer.CPBuffEntries[prior_chkpt].uncompleted_instr_count++;

    return prior_chkpt;
}

void renamer::checkpoint()
{
    //printf("checkpoint()\n");

    // sanity check; should never be empty because there 
    // should always be an "oldest checkpoint" at all times
    assert(!(CPBuffer.head == CPBuffer.tail && CPBuffer.head_pb == CPBuffer.tail_pb));

    // should not be called when full; stall_checkpoint should catch that
    assert(!(CPBuffer.head == CPBuffer.tail && CPBuffer.head_pb != CPBuffer.tail_pb));

    CPBuffer.CPBuffEntries[CPBuffer.tail].RMT_copy                  = RMT;
    CPBuffer.CPBuffEntries[CPBuffer.tail].PRFUnnmappedBits_copy     = PRFUnnmappedBits;
    CPBuffer.CPBuffEntries[CPBuffer.tail].PRFUsageCounter_copy      = PRFUsageCounter;
    //increment usage counter of each Phys Reg in checkpointed RMT
    for(int i=0; i<RMT.size(); i++){
        CPBuffer.CPBuffEntries[CPBuffer.tail].PRFUsageCounter_copy[RMT[i]]++;
    }
    CPBuffer.CPBuffEntries[CPBuffer.tail].uncompleted_instr_count   = 0;
    CPBuffer.CPBuffEntries[CPBuffer.tail].load_count                = 0;
    CPBuffer.CPBuffEntries[CPBuffer.tail].store_count               = 0;
    CPBuffer.CPBuffEntries[CPBuffer.tail].branch_count              = 0;
    CPBuffer.CPBuffEntries[CPBuffer.tail].has_amo_instr             = false;
    CPBuffer.CPBuffEntries[CPBuffer.tail].has_csr_instr             = false;
    CPBuffer.CPBuffEntries[CPBuffer.tail].has_except_instr          = false;

    if (CPBuffer.tail == UNRESOLVED_BRANCHES_SIZE-1){
        CPBuffer.tail = 0;
        CPBuffer.tail_pb = !CPBuffer.tail_pb;
    }else{
        CPBuffer.tail++;
    }

}

//bool renamer::stall_dispatch(uint64_t bundle_inst)
//{
//    //printf("stall_dispatch()\n");
//
//    if(AL.head == AL.tail && AL.head_pb != AL.tail_pb){  //full
//        return true;
//    }else{
//        if(AL.head_pb == AL.tail_pb){
//           if((AL_SIZE - (AL.tail - AL.head)) >= bundle_inst){
//               return false;
//           }else{
//               return true;
//           }
//        }else{
//           if((AL.head - AL.tail) >= bundle_inst){
//               return false;
//           }else{
//               return true;
//           }
//        }
//    }
//}

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

void renamer::set_complete(uint64_t checkpoint_ID)
{
    //printf("set_complete()\n");

    CPBuffer.CPBuffEntries[checkpoint_ID].uncompleted_instr_count--;
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

bool renamer::precommit(uint64_t &chkpt_id, uint64_t &num_loads, uint64_t &num_stores,
                        uint64_t &num_branches, bool &amo, bool &csr, bool &exception)
{
    //printf("precommit(): \n");

    if (CPBuffer.head < UNRESOLVED_BRANCHES_SIZE-2){
        if(CPBuffer.head + 1 == CPBuffer.tail){
            //there is no other checkpoint after oldest checkpoint
            return false;
        }
    }else{ //next entry after head is back to 0
        if(CPBuffer.tail == 0){
            //there is no other checkpoint after oldest checkpoint
            return false;
        }
    }

    if(CPBuffer.CPBuffEntries[CPBuffer.head].uncompleted_instr_count > 0){
        return false;
    }

    chkpt_id        = CPBuffer.head;
    num_loads       = CPBuffer.CPBuffEntries[CPBuffer.head].load_count;
    num_stores      = CPBuffer.CPBuffEntries[CPBuffer.head].store_count;
    num_branches    = CPBuffer.CPBuffEntries[CPBuffer.head].branch_count;
    amo             = CPBuffer.CPBuffEntries[CPBuffer.head].has_amo_instr;
    csr             = CPBuffer.CPBuffEntries[CPBuffer.head].has_csr_instr;
    exception       = CPBuffer.CPBuffEntries[CPBuffer.head].has_except_instr;

    //printf("    %d\n", true);
    return true;
}

void renamer::commit(uint64_t log_reg)
{
    //printf("commit()\n");

    assert(CPBuffer.CPBuffEntries[CPBuffer.head].PRFUsageCounter_copy[RMT[log_reg]] > 0);

    CPBuffer.CPBuffEntries[CPBuffer.head].PRFUsageCounter_copy[RMT[log_reg]]--;
}

void renamer::free_checkpoint()
{
    if (CPBuffer.head == UNRESOLVED_BRANCHES_SIZE-1){
        CPBuffer.head = 0;
        CPBuffer.head_pb = !CPBuffer.head_pb;
    }else{
        CPBuffer.head++;
    }
}

void renamer::squash()
{
    //printf("squash()\n");

    //TODO: why restoring from the oldest? and not from a specific chkpt
    //restoring RMT
    RMT = CPBuffer.CPBuffEntries[CPBuffer.head].RMT_copy;
    PRFUnnmappedBits = CPBuffer.CPBuffEntries[CPBuffer.head].PRFUnnmappedBits_copy;

    //PRFUsageCounter = CPBuffer.CPBuffEntries[CPBuffer.head].PRFUsageCounter_copy;
    //TODO: renitialize??
    //for(uint64_t i=0; i<PHYS_REG_SIZE; i++){
    //    PRFUsageCounter[i] = 0;
    //}

    //FL.head = 0;                                 
    //FL.tail = 0;
    //FL.head_pb = false;     // 0 ]___\ full
    //FL.tail_pb = true;      // 1 ]   /
    //// intially the remainder of Phys Regs are free (n_phys_regs (allocated in RMT&AMT) - n_log_regs)
    //for(uint64_t i=LOGREG_RMT_AMT_SIZE; i<PHYS_REG_SIZE; i++){
    //    FL.fl_regs.push_back(i); 
    //}



    //restoring FL to full
//    FL.head = FL.tail;
//    FL.head_pb = !FL.tail_pb;

//    for(uint64_t i=0; i<PHYS_REG_SIZE; i++){
//        //update PRF ready bit to indicate that phys_reg is ready to be picked up
//        PRF_rb[i] = true;
//    }
// TODO: may no longer need

}

void renamer::set_exception(uint64_t checkpoint_ID)
{
    //printf("set_exception()\n");

    CPBuffer.CPBuffEntries[checkpoint_ID].has_except_instr = true;
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
