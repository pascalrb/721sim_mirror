#include "pipeline.h"


////////////////////////////////////////////////////////////////////////////////////
// The Rename Stage has two sub-stages:
// rename1: Get the next rename bundle from the FQ.
// rename2: Rename the current rename bundle.
////////////////////////////////////////////////////////////////////////////////////

void pipeline_t::rename1() {
   unsigned int i;
   unsigned int rename1_bundle_width;

   ////////////////////////////////////////////////////////////////////////////////////
   // Try to get the next rename bundle.
   // Two conditions might prevent getting the next rename bundle, either:
   // (1) The current rename bundle is stalled in rename2.
   // (2) The FQ does not have enough instructions for a full rename bundle,
   //     and it's not because the fetch unit is stalled waiting for a
   //     serializing instruction to retire (fetch exception, amo, or csr instruction).
   ////////////////////////////////////////////////////////////////////////////////////

   // Check the first condition. Is the current rename bundle stalled, preventing
   // insertion of the next rename bundle? Check whether or not the pipeline register
   // between rename1 and rename2 still has a rename bundle.

   if (RENAME2[0].valid) {	// The current rename bundle is stalled.
      return;
   }

   // Check the second condition.
   // Stall if the fetch unit is active (it's not waiting for a serializing
   // instruction to retire) and the FQ doesn't have enough instructions for a full
   // rename bundle.

   rename1_bundle_width = ((FQ.get_length() < dispatch_width) ? FQ.get_length() : dispatch_width);

   if (FetchUnit->active() && (rename1_bundle_width < dispatch_width)) {
      return;
   }

   // Get the next rename bundle.
   for (i = 0; i < rename1_bundle_width; i++) {
      assert(!RENAME2[i].valid);
      RENAME2[i].valid = true;
      RENAME2[i].index = FQ.pop();
   }
}

void pipeline_t::rename2() {
   unsigned int i, instr_bw_chkpt;
   unsigned int index;
   unsigned int bundle_dst, bundle_chkpts;
   db_t * actual;

   // Stall the rename2 sub-stage if either:
   // (1) There isn't a current rename bundle.
   // (2) The Dispatch Stage is stalled.
   // (3) There aren't enough rename resources for the current rename bundle.

   if (!RENAME2[0].valid ||	// First stall condition: There isn't a current rename bundle.
       DISPATCH[0].valid) {	// Second stall condition: The Dispatch Stage is stalled.
      return;
   }

   // Third stall condition: There aren't enough rename resources for the current rename bundle.
   bundle_dst = 0;
   bundle_chkpts = 0;
   instr_bw_chkpt = instr_renamed_since_last_checkpoint;
   for (i = 0; i < dispatch_width; i++) {
      if (!RENAME2[i].valid)
         break;			// Not a valid instruction: Reached the end of the rename bundle so exit loop.

      index = RENAME2[i].index;

      // FIX_ME #1
      // Count the number of instructions in the rename bundle that need a checkpoint (most branches).
      // Count the number of instructions in the rename bundle that have a destination register.
      // With these counts, you will be able to query the renamer for resource availability
      // (checkpoints and physical registers).
      //
      // Tips:
      // 1. The loop construct, for iterating through all instructions in the rename bundle (0 to dispatch_width),
      //    is already provided for you, above. Note that this comment is within the loop.
      // 2. At this point of the code, 'index' is the instruction's index into PAY.buf[] (payload).
      // 3. The instruction's payload has all the information you need to count resource needs.
      //    There is a flag in the instruction's payload that *directly* tells you if this instruction needs a checkpoint.
      //    Another field indicates whether or not the instruction has a destination register.
      //TODO: CPR update comment

      // FIX_ME #1 BEGIN
      if(PAY.buf[index].C_valid){
         bundle_dst++;
      }
      // FIX_ME #1 END

      //TODO: CPR might need to revise this logic
      if(IS_AMO(PAY.buf[index].flags) || IS_CSR(PAY.buf[index].flags)){
         if(instr_bw_chkpt == 0){
            bundle_chkpts += 1;
         }else{
            bundle_chkpts += 2;
         }

         instr_bw_chkpt = 0;

      }else if(PAY.buf[index].good_instruction){
         actual = get_pipe()->peek(PAY.buf[index].db_index);

         if(actual->a_exception || (actual->a_next_pc != PAY.buf[index].next_pc)){      
            if(instr_bw_chkpt != 0){
               bundle_chkpts++;
            }
            
            instr_bw_chkpt = 0;
         }
      }
      
      if (instr_bw_chkpt == max_instr_bw_checkpoints){
         bundle_chkpts++;

         instr_bw_chkpt = 0;
      }

      instr_bw_chkpt++;
      //TODO: CPR low confidence branch
   }


   // FIX_ME #2
   // Check if the Rename2 Stage must stall due to any of the following conditions:
   // * Not enough free checkpoints.
   // * Not enough free physical registers.
   //
   // If there are not enough resources for the *whole* rename bundle, then stall the Rename2 Stage.
   // Stalling is achieved by returning from this function ('return').
   // If there are enough resources for the *whole* rename bundle, then do not stall the Rename2 Stage.
   // This is achieved by doing nothing and proceeding to the next statements.

   // FIX_ME #2 BEGIN
   if(REN->stall_checkpoint(bundle_chkpts)){
      return;
   }
   if(REN->stall_reg(bundle_dst)){
      return;
   }
   // FIX_ME #2 END

   //
   // Sufficient resources are available to rename the rename bundle.
   //
   for (i = 0; i < dispatch_width; i++) {
      if (!RENAME2[i].valid)
         break;			// Not a valid instruction: Reached the end of the rename bundle so exit loop.

      index = RENAME2[i].index;

      // FIX_ME #3
      // Rename source registers (first) and destination register (second).
      //
      // Tips:
      // 1. At this point of the code, 'index' is the instruction's index into PAY.buf[] (payload).
      // 2. The instruction's payload has all the information you need to rename registers, if they exist. In particular:
      //    * whether or not the instruction has a first source register, and its logical register number
      //    * whether or not the instruction has a second source register, and its logical register number
      //    * whether or not the instruction has a third source register, and its logical register number
      //    * whether or not the instruction has a destination register, and its logical register number
      // 3. When you rename a logical register to a physical register, remember to *update* the instruction's payload with the physical register specifier,
      //    so that the physical register specifier can be used in subsequent pipeline stages.

      //TODO: CPR do I set PAY.buf[index].checkpoint to true after each checkpoint???

      if(IS_AMO(PAY.buf[index].flags) || IS_CSR(PAY.buf[index].flags)){
         if(instr_renamed_since_last_checkpoint > 0){
            REN->checkpoint();
            instr_renamed_since_last_checkpoint = 0;  
         }
      }else if(PAY.buf[index].good_instruction){
         actual = get_pipe()->peek(PAY.buf[index].db_index);
         if(actual->a_exception){
            if(instr_renamed_since_last_checkpoint > 0){
               REN->checkpoint();
               instr_renamed_since_last_checkpoint = 0;  
            }
         }
      }

      // FIX_ME #3 BEGIN
      if(PAY.buf[index].A_valid){
         PAY.buf[index].A_phys_reg = REN->rename_rsrc(PAY.buf[index].A_log_reg);
      }
      if(PAY.buf[index].B_valid){
         PAY.buf[index].B_phys_reg = REN->rename_rsrc(PAY.buf[index].B_log_reg);
      }
      if(PAY.buf[index].D_valid){
         PAY.buf[index].D_phys_reg = REN->rename_rsrc(PAY.buf[index].D_log_reg);
      }
      if(PAY.buf[index].C_valid){
         PAY.buf[index].C_phys_reg = REN->rename_rdst(PAY.buf[index].C_log_reg);
      }
      // FIX_ME #3 END

      instr_renamed_since_last_checkpoint++;

      PAY.buf[index].checkpoint_ID = REN->get_checkpoint_ID(IS_LOAD(PAY.buf[index].flags), 
                              IS_STORE(PAY.buf[index].flags), IS_BRANCH(PAY.buf[index].flags), 
                              IS_AMO(PAY.buf[index].flags), IS_CSR(PAY.buf[index].flags));

      RENAME2[i].checkpoint_ID = PAY.buf[index].checkpoint_ID;

      if(PAY.buf[index].good_instruction){
         actual = get_pipe()->peek(PAY.buf[index].db_index);
         if(actual->a_exception){
            REN->set_exception(PAY.buf[index].checkpoint_ID);
         }
      }

      if(IS_AMO(PAY.buf[index].flags) || IS_CSR(PAY.buf[index].flags)
            || instr_renamed_since_last_checkpoint == max_instr_bw_checkpoints){

         if(instr_renamed_since_last_checkpoint > 0){
            REN->checkpoint();
            instr_renamed_since_last_checkpoint = 0;  
         }
      }else if(PAY.buf[index].good_instruction){
         actual = get_pipe()->peek(PAY.buf[index].db_index);
         if(actual->a_next_pc != PAY.buf[index].next_pc){
            if(instr_renamed_since_last_checkpoint > 0){
               REN->checkpoint();
               instr_renamed_since_last_checkpoint = 0;  
            }
         }
      }
      //TODO: CPR low confidence branch


   // FIX_ME #4
   // Get the instruction's branch mask.
   //
   // Tips:
   // 1. Every instruction gets a branch_mask. An instruction needs to know which branches it depends on, for possible squashing.
   // 2. The branch_mask is not held in the instruction's PAY.buf[] entry. Rather, it explicitly moves with the instruction
   //    from one pipeline stage to the next. Normally the branch_mask would be wires at this point in the logic but since we
   //    don't have wires place it temporarily in the RENAME2[] pipeline register alongside the instruction, until it advances
   //    to the DISPATCH[] pipeline register. The required left-hand side of the assignment statement is already provided for you below:
   //    RENAME2[i].branch_mask = ??;

   // FIX_ME #4 BEGIN
   //RENAME2[i].branch_mask = REN->get_branch_mask();
   //TODO: CPR may no longer need; delete member from structure!
   // FIX_ME #4 END
   

//      // FIX_ME #5
//      // If this instruction requires a checkpoint (most branches), then create a checkpoint.
//      //
//      // Tips:
//      // 1. At this point of the code, 'index' is the instruction's index into PAY.buf[] (payload).
//      // 2. There is a flag in the instruction's payload that *directly* tells you if this instruction needs a checkpoint.
//      // 3. If you create a checkpoint, remember to *update* the instruction's payload with its branch ID
//      //    so that the branch ID can be used in subsequent pipeline stages.
//
//      // FIX_ME #5 BEGIN
//      if(PAY.buf[index].checkpoint){
//         PAY.buf[index].branch_ID = REN->checkpoint();
//         //TODO: CPR may need to delete 
//      }
//      // FIX_ME #5 END
   }

   //
   // Transfer the rename bundle from the Rename Stage to the Dispatch Stage.
   //
   for (i = 0; i < dispatch_width; i++) {
      if (!RENAME2[i].valid)
         break;			// Not a valid instruction: Reached the end of the rename bundle so exit loop.

      assert(!DISPATCH[i].valid);
      RENAME2[i].valid = false;
      DISPATCH[i].valid = true;
      DISPATCH[i].index = RENAME2[i].index;
      DISPATCH[i].checkpoint_ID = RENAME2[i].checkpoint_ID;
   }
}
