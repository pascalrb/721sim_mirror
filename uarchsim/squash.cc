#include "pipeline.h"


void pipeline_t::squash_complete(reg_t jump_PC) {
	unsigned int i, j;

	//////////////////////////
	// Fetch Stage
	//////////////////////////
  
	FetchUnit->flush(jump_PC);

	//////////////////////////
	// Decode Stage
	//////////////////////////

	for (i = 0; i < fetch_width; i++) {
		DECODE[i].valid = false;
	}

	//////////////////////////
	// Rename1 Stage
	//////////////////////////

	FQ.flush();

	//////////////////////////
	// Rename2 Stage
	//////////////////////////

	for (i = 0; i < dispatch_width; i++) {
		RENAME2[i].valid = false;
	}

	//
	// FIX_ME #17c
	// Squash the renamer.
	//

	// FIX_ME #17c BEGIN
	REN->squash();
	// FIX_ME #17c END


	//////////////////////////
	// Dispatch Stage
	//////////////////////////

	for (i = 0; i < dispatch_width; i++) {
		if(DISPATCH[i].valid){
			DISPATCH[i].valid = false;
			if(PAY.buf[DISPATCH[i].index].A_valid){
				REN->dec_usage_counter(PAY.buf[DISPATCH[i].index].A_phys_reg);
			}
			if(PAY.buf[DISPATCH[i].index].B_valid){
				REN->dec_usage_counter(PAY.buf[DISPATCH[i].index].B_phys_reg);
			}
			if(PAY.buf[DISPATCH[i].index].D_valid){
				REN->dec_usage_counter(PAY.buf[DISPATCH[i].index].D_phys_reg);
			}
			if(PAY.buf[DISPATCH[i].index].C_valid){
				REN->dec_usage_counter(PAY.buf[DISPATCH[i].index].C_phys_reg);
			}
		}
	}

	//////////////////////////
	// Schedule Stage
	//////////////////////////

	IQ.flush();

	//////////////////////////
	// Register Read Stage
	// Execute Stage
	// Writeback Stage
	//////////////////////////

	for (i = 0; i < issue_width; i++) {
		if (Execution_Lanes[i].rr.valid) {
			Execution_Lanes[i].rr.valid = false;

			if(PAY.buf[Execution_Lanes[i].rr.index].A_valid){
				REN->dec_usage_counter(PAY.buf[Execution_Lanes[i].rr.index].A_phys_reg);
			}
			if(PAY.buf[Execution_Lanes[i].rr.index].B_valid){
				REN->dec_usage_counter(PAY.buf[Execution_Lanes[i].rr.index].B_phys_reg);
			}
			if(PAY.buf[Execution_Lanes[i].rr.index].D_valid){
				REN->dec_usage_counter(PAY.buf[Execution_Lanes[i].rr.index].D_phys_reg);
			}
			if(PAY.buf[Execution_Lanes[i].rr.index].C_valid){
				REN->dec_usage_counter(PAY.buf[Execution_Lanes[i].rr.index].C_phys_reg);
			}
		}

		for (j = 0; j < Execution_Lanes[i].ex_depth; j++){
		   Execution_Lanes[i].ex[j].valid = false;

			if (Execution_Lanes[i].ex[j].valid) {
				if(PAY.buf[Execution_Lanes[i].ex[j].index].A_valid){
					REN->dec_usage_counter(PAY.buf[Execution_Lanes[i].ex[j].index].A_phys_reg);
				}
				if(PAY.buf[Execution_Lanes[i].ex[j].index].B_valid){
					REN->dec_usage_counter(PAY.buf[Execution_Lanes[i].ex[j].index].B_phys_reg);
				}
				if(PAY.buf[Execution_Lanes[i].ex[j].index].D_valid){
					REN->dec_usage_counter(PAY.buf[Execution_Lanes[i].ex[j].index].D_phys_reg);
				}
				if(PAY.buf[Execution_Lanes[i].ex[j].index].C_valid){
					REN->dec_usage_counter(PAY.buf[Execution_Lanes[i].ex[j].index].C_phys_reg);
				}
			}
		}
		if (Execution_Lanes[i].wb.valid) {
			Execution_Lanes[i].wb.valid = false;

			if(PAY.buf[Execution_Lanes[i].wb.index].A_valid){
				REN->dec_usage_counter(PAY.buf[Execution_Lanes[i].wb.index].A_phys_reg);
			}
			if(PAY.buf[Execution_Lanes[i].ex[j].index].B_valid){
				REN->dec_usage_counter(PAY.buf[Execution_Lanes[i].wb.index].B_phys_reg);
			}
			if(PAY.buf[Execution_Lanes[i].ex[j].index].D_valid){
				REN->dec_usage_counter(PAY.buf[Execution_Lanes[i].wb.index].D_phys_reg);
			}
			if(PAY.buf[Execution_Lanes[i].ex[j].index].C_valid){
				REN->dec_usage_counter(PAY.buf[Execution_Lanes[i].wb.index].C_phys_reg);
			}
		}
	}

	LSU.flush();
}


void pipeline_t::selective_squash(uint64_t squash_mask) {
	unsigned int i, j;

	// Squash all instructions in the Decode through Dispatch Stages.
	// Full squash front end. Selectively squash IQ & execution lanes 

	// Decode Stage:
	for (i = 0; i < fetch_width; i++) {
		DECODE[i].valid = false;
	}

	// Rename1 Stage:
	FQ.flush();

	// Rename2 Stage:
	for (i = 0; i < dispatch_width; i++) {
		RENAME2[i].valid = false;
		
		//TODO: CPR selectively decrement usage counter?
		// When an instruction (that's part of a bundle) executes into an offending
		// 	instruction, what happens to the bundle that were renamed?
		//		(execute() stage gets called before rename() stage so may not need to 
		//		cover this case. that "rename" bundle would actually be in the dispatch 
		//		pipeline reg, which is covered below) 	
	}

	// Dispatch Stage:
	for (i = 0; i < dispatch_width; i++) {
		if(DISPATCH[i].valid && IS_CHKPT_IN_MASK(DISPATCH[i].checkpoint_ID, squash_mask)){

			if(PAY.buf[DISPATCH[i].index].A_valid){
				REN->dec_usage_counter(PAY.buf[DISPATCH[i].index].A_phys_reg);
			}
			if(PAY.buf[DISPATCH[i].index].B_valid){
				REN->dec_usage_counter(PAY.buf[DISPATCH[i].index].B_phys_reg);
			}
			if(PAY.buf[DISPATCH[i].index].D_valid){
				REN->dec_usage_counter(PAY.buf[DISPATCH[i].index].D_phys_reg);
			}
			if(PAY.buf[DISPATCH[i].index].C_valid){
				REN->dec_usage_counter(PAY.buf[DISPATCH[i].index].C_phys_reg);
			}

			DISPATCH[i].valid = false;
		}
	}

	// Selectively squash instructions after the branch, in the Schedule through Writeback Stages.

	// Schedule Stage:
	IQ.squash(squash_mask);

	for (i = 0; i < issue_width; i++) {
		// Register Read Stage:
		if (Execution_Lanes[i].rr.valid && IS_CHKPT_IN_MASK(Execution_Lanes[i].rr.checkpoint_ID, squash_mask)) {
			if(PAY.buf[Execution_Lanes[i].rr.index].A_valid){
				REN->dec_usage_counter(PAY.buf[Execution_Lanes[i].rr.index].A_phys_reg);
			}
			if(PAY.buf[Execution_Lanes[i].rr.index].B_valid){
				REN->dec_usage_counter(PAY.buf[Execution_Lanes[i].rr.index].B_phys_reg);
			}
			if(PAY.buf[Execution_Lanes[i].rr.index].D_valid){
				REN->dec_usage_counter(PAY.buf[Execution_Lanes[i].rr.index].D_phys_reg);
			}
			if(PAY.buf[Execution_Lanes[i].rr.index].C_valid){
				REN->dec_usage_counter(PAY.buf[Execution_Lanes[i].rr.index].C_phys_reg);
			}

			Execution_Lanes[i].rr.valid = false;
		}

		// Execute Stage:
		for (j = 0; j < Execution_Lanes[i].ex_depth; j++) {
			if (Execution_Lanes[i].ex[j].valid && IS_CHKPT_IN_MASK(Execution_Lanes[i].ex[j].checkpoint_ID, squash_mask)) {
				if(PAY.buf[Execution_Lanes[i].ex[j].index].A_valid){
					REN->dec_usage_counter(PAY.buf[Execution_Lanes[i].ex[j].index].A_phys_reg);
				}
				if(PAY.buf[Execution_Lanes[i].ex[j].index].B_valid){
					REN->dec_usage_counter(PAY.buf[Execution_Lanes[i].ex[j].index].B_phys_reg);
				}
				if(PAY.buf[Execution_Lanes[i].ex[j].index].D_valid){
					REN->dec_usage_counter(PAY.buf[Execution_Lanes[i].ex[j].index].D_phys_reg);
				}
				if(PAY.buf[Execution_Lanes[i].ex[j].index].C_valid){
					REN->dec_usage_counter(PAY.buf[Execution_Lanes[i].ex[j].index].C_phys_reg);
				}

				Execution_Lanes[i].ex[j].valid = false;
			}
		}

		// Writeback Stage:
		if (Execution_Lanes[i].wb.valid && IS_CHKPT_IN_MASK(Execution_Lanes[i].wb.checkpoint_ID, squash_mask)) {
			if(PAY.buf[Execution_Lanes[i].wb.index].A_valid){
				REN->dec_usage_counter(PAY.buf[Execution_Lanes[i].wb.index].A_phys_reg);
			}
			if(PAY.buf[Execution_Lanes[i].ex[j].index].B_valid){
				REN->dec_usage_counter(PAY.buf[Execution_Lanes[i].wb.index].B_phys_reg);
			}
			if(PAY.buf[Execution_Lanes[i].ex[j].index].D_valid){
				REN->dec_usage_counter(PAY.buf[Execution_Lanes[i].wb.index].D_phys_reg);
			}
			if(PAY.buf[Execution_Lanes[i].ex[j].index].C_valid){
				REN->dec_usage_counter(PAY.buf[Execution_Lanes[i].wb.index].C_phys_reg);
			}

			Execution_Lanes[i].wb.valid = false;
		}
	}
	
	//TODO: CPR should LSU also be selectively_squashed??
}
