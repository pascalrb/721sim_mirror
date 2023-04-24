#include "renamer.h"

#include <assert.h>     
#include <stdio.h>  //p_rintf
#include <iostream> //c_out
#include <algorithm>    // std::find

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
    //assert(1 <= n_branches && n_branches <= MAX_UNRESOLVED_BRANCHES);
    assert(n_active > 0);

    // initializing private vars  
    //TODO: CPR change name of var
    LOGREG_RMT_AMT_SIZE         = n_log_regs;
    PHYS_REG_SIZE               = n_phys_regs;
    UNRESOLVED_BRANCHES_SIZE    = n_branches;
    //AL_SIZE                     = n_active;       //TODO: CPR may no longer need
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
        PRFUsageCounter[i]++;

        //Phys Regs mapped to RMT need their unmapped bit set to false
        PRFUnnmappedBits[i] = false;
    }
    
    // Free List init
    // intially the remainder of Phys Regs are free (n_phys_regs (allocated in RMT&AMT) - n_log_regs)
    for(uint64_t i=LOGREG_RMT_AMT_SIZE; i<PHYS_REG_SIZE; i++){
        FL.push_back(i); 
    }
    db_fl_size = FL.size();

    // Initialize first "oldest" checkpoint buffer
    CPBuffer.head = 0;                                   
    CPBuffer.tail = 0;
    CPBuffer.head_pb = false;     // 0 ]___\ empty
    CPBuffer.tail_pb = false;     // 0 ]   /
    CPBuffer.CPBuffEntries.resize(n_branches);

    CPBuffer.CPBuffEntries[CPBuffer.tail].RMT_copy                  = RMT;
    CPBuffer.CPBuffEntries[CPBuffer.tail].PRFUnnmappedBits_copy     = PRFUnnmappedBits;
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
    //TODO: CPR debug. remove after
    db_fl_size = FL.size();

    if(FL.empty()){
        return true;
    }else{
        if(((int64_t) FL.size() - (int64_t) bundle_dst) < 0){
            return true;
        }else{
            return false;
        }
    }
}

uint64_t renamer::rename_rsrc(uint64_t log_reg)
{
    //printf("rename_rsrc()\n");

    uint64_t phys_reg = RMT[log_reg];

    //Update usage counter corresponding to the phys reg
    inc_usage_counter(phys_reg);

    return phys_reg;
}

uint64_t renamer::rename_rdst(uint64_t log_reg)
{
    //printf("rename_rdst()\n");

    //unmap previous value
    unmap(RMT[log_reg]);

    int tmp = db_fl_size;

    assert(!FL.empty());

    uint64_t phys_reg = FL.back();
    FL.pop_back();
    db_fl_size = FL.size();

    //Update usage counter and mapping corresponding to the phys reg
    map(phys_reg);
    inc_usage_counter(phys_reg);

    //update RMT
    RMT[log_reg] = phys_reg;

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

    CPBuffer.CPBuffEntries[prior_chkpt].has_amo_instr = amo;
    CPBuffer.CPBuffEntries[prior_chkpt].has_csr_instr = csr;

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

    CPBuffer.CPBuffEntries[CPBuffer.tail].RMT_copy = RMT;
    //PRFUnnmappedBits = CPBuffer.CPBuffEntries[CPBuffer.tail].PRFUnnmappedBits_copy;
    CPBuffer.CPBuffEntries[CPBuffer.tail].PRFUnnmappedBits_copy = PRFUnnmappedBits;

    //increment usage counter of each Phys Reg in checkpointed RMT
    for(int i=0; i<RMT.size(); i++){
        inc_usage_counter(RMT[i]);
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

    dec_usage_counter(phys_reg);
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
    dec_usage_counter(phys_reg);
}

void renamer::set_complete(uint64_t checkpoint_ID)
{
    //printf("set_complete()\n");

    assert(CPBuffer.CPBuffEntries[checkpoint_ID].uncompleted_instr_count > 0);

    CPBuffer.CPBuffEntries[checkpoint_ID].uncompleted_instr_count--;
}


bool renamer::precommit(uint64_t &chkpt_id, uint64_t &num_loads, uint64_t &num_stores,
                        uint64_t &num_branches, bool &amo, bool &csr, bool &exception)
{
    //printf("precommit(): \n");

    if(CPBuffer.CPBuffEntries[CPBuffer.head].uncompleted_instr_count != 0){
        return false;
    }

    if(!CPBuffer.CPBuffEntries[CPBuffer.head].has_except_instr){

        //check if there is a valid checkpoint after oldest checkpoint
        if (CPBuffer.head < UNRESOLVED_BRANCHES_SIZE-1){
            if(CPBuffer.head + 1 == CPBuffer.tail){
                //there is no other valid checkpoint after oldest checkpoint
                return false;
            }
        }else{ //next entry after head is back to 0
            if(CPBuffer.tail == 0){
                //there is no other valid checkpoint after oldest checkpoint
                return false;
            }
        }
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

    dec_usage_counter(CPBuffer.CPBuffEntries[CPBuffer.head].RMT_copy[log_reg]);
}

void renamer::free_checkpoint()
{
    //check if there is a valid checkpoint after oldest checkpoint
    if (CPBuffer.head < UNRESOLVED_BRANCHES_SIZE-1){
        if(CPBuffer.head + 1 == CPBuffer.tail){
            //there is no other valid checkpoint after oldest checkpoint
            assert(0);
        }
    }else{ //next entry after head is back to 0
        if(CPBuffer.tail == 0){
            //there is no other valid checkpoint after oldest checkpoint
            assert(0);
        }
    }
    assert(CPBuffer.CPBuffEntries[CPBuffer.head].uncompleted_instr_count == 0);

    if (CPBuffer.head == UNRESOLVED_BRANCHES_SIZE-1){
        CPBuffer.head = 0;
        CPBuffer.head_pb = !CPBuffer.head_pb;
    }else{
        CPBuffer.head++;
    }
}

void renamer::inc_usage_counter(uint64_t phys_reg)
{
    PRFUsageCounter[phys_reg]++;
}

void renamer::dec_usage_counter(uint64_t phys_reg)
{
    //TODO: CPR this blocks the program but for some reason 
    //  removing it passes it
    assert(PRFUsageCounter[phys_reg] > 0);

    PRFUsageCounter[phys_reg]--;

    //TODO: CPR debug. remove after
    int db_use;
    db_use = PRFUsageCounter[phys_reg];

    //Aggressive Register reclammation
    try_reg_reclamation(phys_reg);
}

void renamer::map(uint64_t phys_reg)
{
    PRFUnnmappedBits[phys_reg] = false;
}
void renamer::unmap(uint64_t phys_reg)
{
    //Only check if unmapped bit is going from 0 to 1
    // otherwise, it's already unmapped, no need to try_reg_reclamation()
    //assert(!PRFUnnmappedBits[phys_reg]);
    if(!PRFUnnmappedBits[phys_reg]){
        PRFUnnmappedBits[phys_reg] = true;

        //TODO: CPR can this be called multiple times for the same reg??
        //  (the same phys reg can be 1-0 for multiple times that unmap 
        // gets called. This will lead to duplicate push to FL)
        try_reg_reclamation(phys_reg);
    }
}

void renamer::try_reg_reclamation(uint64_t phys_reg)
{
    if(PRFUnnmappedBits[phys_reg] && PRFUsageCounter[phys_reg] == 0){
        //TODO: CPR debug assert; will slow prog; remove after project is up and running;
        // assert that phys_reg is not already in FL
        assert(std::find(FL.begin(), FL.end(), phys_reg) == FL.end());
        
        //TODO: CPR debug. remove after
        //int db_fl_size;
        
        //asserting that we're not too aggressive 
        assert(FL.size() < FL_SIZE);

        FL.push_back(phys_reg);
        db_fl_size = FL.size();
    }
}

uint64_t renamer::rollback(uint64_t chkpt_id, bool next, uint64_t &total_loads,
				           uint64_t &total_stores, uint64_t &total_branches)
{
    //printf("resolve() - squash_mask: %lu\n", squash_mask);

    uint64_t squash_mask, to_tail, tmp_chkpt;

    if(next){
        if (chkpt_id == UNRESOLVED_BRANCHES_SIZE-1){
            chkpt_id = 0;
        }else{
            chkpt_id++;
        }
    }

    // assert checkpoint exists (in between CPBuffer head and tail)
    if(CPBuffer.head_pb == CPBuffer.tail_pb){
        //head --> tail
        assert(chkpt_id >= CPBuffer.head && chkpt_id < CPBuffer.tail);
    }else{
        //tail --> head (wrapped around)
        if(CPBuffer.head != CPBuffer.tail){
            assert(chkpt_id >= CPBuffer.head || chkpt_id < CPBuffer.tail);
        }
        //if full (head == tail), chkpt_id can be anywhere
    }

    //checkpointing 
    RMT = CPBuffer.CPBuffEntries[chkpt_id].RMT_copy;
    //PRFUnnmappedBits = CPBuffer.CPBuffEntries[chkpt_id].PRFUnnmappedBits_copy;
    for(int i = 0; i < PRFUnnmappedBits.size(); i++){
        if(CPBuffer.CPBuffEntries[chkpt_id].PRFUnnmappedBits_copy[i]){
            unmap(PRFUnnmappedBits[i]);
        }else{
            map(PRFUnnmappedBits[i]);
        }
    }
    
    //Decrement the usage counter of each physical reg mapped in each squashed/freed chkpt
    tmp_chkpt = chkpt_id;
    //preserving the chkpt_id checkpoint by starting at next entry after chkpt_id
    if (tmp_chkpt == UNRESOLVED_BRANCHES_SIZE-1){
        tmp_chkpt = 0;
    }else{
        tmp_chkpt++;
    }
    while(tmp_chkpt != CPBuffer.tail){
        for(int i=0; i<RMT.size(); i++){
            dec_usage_counter(CPBuffer.CPBuffEntries[tmp_chkpt].RMT_copy[i]);
        }

        if (tmp_chkpt == UNRESOLVED_BRANCHES_SIZE-1){
            tmp_chkpt = 0;
        }else{
            tmp_chkpt++;
        }
    }

    
    //reseting the checkpoint's member vars
    CPBuffer.CPBuffEntries[chkpt_id].uncompleted_instr_count    = 0;
    CPBuffer.CPBuffEntries[chkpt_id].load_count                 = 0;
    CPBuffer.CPBuffEntries[chkpt_id].store_count                = 0;
    CPBuffer.CPBuffEntries[chkpt_id].branch_count               = 0;
    CPBuffer.CPBuffEntries[chkpt_id].has_amo_instr              = false;
    CPBuffer.CPBuffEntries[chkpt_id].has_csr_instr              = false;
    CPBuffer.CPBuffEntries[chkpt_id].has_except_instr           = false;

    //grabbing total loads, stores, branch count not squashed checkpoints
    tmp_chkpt = CPBuffer.head;
    while(tmp_chkpt != chkpt_id){
        total_loads += CPBuffer.CPBuffEntries[tmp_chkpt].load_count; 
        total_stores += CPBuffer.CPBuffEntries[tmp_chkpt].store_count; 
        total_branches += CPBuffer.CPBuffEntries[tmp_chkpt].branch_count; 

        if (tmp_chkpt == UNRESOLVED_BRANCHES_SIZE-1){
            tmp_chkpt = 0;
        }else{
            tmp_chkpt++;
        }
    }

    //TODO: CPR do we need to update the PRF_rb?
    //for(uint64_t i=0; i<PHYS_REG_SIZE; i++){
    //    //update PRF ready bit to indicate that phys_reg is ready to be picked up
    //    set_ready(i);
    //}


    // TODO: CPR Move up for legibility
    // generating squash mask from chkpt_id to tail
    squash_mask = ~((1 << chkpt_id) - 1);
    to_tail = (1 << CPBuffer.tail) - 1;
    if(chkpt_id < CPBuffer.tail){
        //head --> tail
        squash_mask &= to_tail;
    }else{
        //tail --> head (wrapped around)
        squash_mask |= to_tail;
    }

    //Actually rollback the tail to the chkpt_id
    if(CPBuffer.tail < chkpt_id){
        CPBuffer.tail_pb = !CPBuffer.tail_pb;
    }
    CPBuffer.tail = chkpt_id;

    
    return squash_mask;
}

//Complete squash of the renamer 
void renamer::squash()
{
    //printf("squash()\n");

    //checkpointing the RMT and Unmapped bit vector
    RMT = CPBuffer.CPBuffEntries[CPBuffer.head].RMT_copy;
    //PRFUnnmappedBits = CPBuffer.CPBuffEntries[CPBuffer.head].PRFUnnmappedBits_copy;
    for(int i = 0; i < PRFUnnmappedBits.size(); i++){
        if(CPBuffer.CPBuffEntries[CPBuffer.head].PRFUnnmappedBits_copy[i]){
            unmap(PRFUnnmappedBits[i]);
        }else{
            map(PRFUnnmappedBits[i]);
        }
    }

    uint64_t tmp_chkpt, tmp_head_plus_1;
    tmp_chkpt = CPBuffer.head;
    //preserving the oldest checkpoint by starting at next entry after chkpt_id
    if (tmp_chkpt == UNRESOLVED_BRANCHES_SIZE-1){
        tmp_chkpt = 0;
    }else{
        tmp_chkpt++;
    }
    tmp_head_plus_1 = tmp_chkpt;

    //THE REAL HARDWARE WAY - naturally freeing register 
    while(tmp_chkpt != CPBuffer.tail){
        for(int i=0; i<RMT.size(); i++){
            //This will automatically free regs, pushing them into the FL
            dec_usage_counter(CPBuffer.CPBuffEntries[tmp_chkpt].RMT_copy[i]);
        }
    
        if (tmp_chkpt == UNRESOLVED_BRANCHES_SIZE-1){
            tmp_chkpt = 0;
        }else{
            tmp_chkpt++;
        }
    }

    //Actually rollback the tail to head+1
    if(CPBuffer.tail < tmp_head_plus_1){
        CPBuffer.tail_pb = !CPBuffer.tail_pb;
    }
    CPBuffer.tail = tmp_head_plus_1;

    //for(uint64_t i=0; i<PHYS_REG_SIZE; i++){
    //    //update PRF ready bit to indicate that all of the phys regs are ready to be picked up
    //    set_ready(i);
    //}

}

void renamer::set_exception(uint64_t checkpoint_ID)
{
    //printf("set_exception()\n");

    CPBuffer.CPBuffEntries[checkpoint_ID].has_except_instr = true;
}

void renamer::set_load_violation(uint64_t checkpoint_ID)
{
    //printf("set_load_violation()\n");

    //TODO: CPR For now, in phase 1, there should not be any load violations 
    //      hence no call to this function
    assert(0);

    //CPBuffer.CPBuffEntries[checkpoint_ID].is_load_violated = true;
    CPBuffer.CPBuffEntries[checkpoint_ID].has_except_instr = true;
}

//void renamer::set_branch_misprediction(uint64_t AL_index)
//{
//    //printf("set_branch_misprediction()\n");
//
//    AL.AL_entries[AL_index].is_branch_mispredicted = true;
//}
//
//void renamer::set_value_misprediction(uint64_t AL_index)
//{
//    //printf("set_value_misprediction()\n");
//
//    AL.AL_entries[AL_index].is_value_mispredicted = true;
//}

bool renamer::get_exception(uint64_t checkpoint_ID)
{
    //printf("get_exception()\n");

    return CPBuffer.CPBuffEntries[checkpoint_ID].has_except_instr;
}
