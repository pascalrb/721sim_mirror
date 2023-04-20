#include <inttypes.h>
#include <vector>

using namespace std;

#define MAX_UNRESOLVED_BRANCHES 8 //TODO: CPR change; was 64

class renamer {
private:
	/////////////////////////////////////////////////////////////////////
	// Put private class variables here.
	/////////////////////////////////////////////////////////////////////
    uint64_t debug_cnt;

    uint64_t LOGREG_RMT_AMT_SIZE;           // Number of entries in Logical Registers, 
                                                // Rename Map Table, Architectural Map Table
    uint64_t PHYS_REG_SIZE;                 // Number of entries in Physical Registers 
    uint64_t UNRESOLVED_BRANCHES_SIZE;      // Number of entries in Branch Checkpoint vector
    //uint64_t AL_SIZE;                       // Number of entries in Active List
    uint64_t FL_SIZE;                       // Number of entries in Free List


    // log_regs: 64, phys_regs: 320, AL_Size: 256

    //TODO: make sure all ^^ are initialized

	//TODO: review all the TODOs in renamer.h and renamer.cc
	//TODO: order the "Structure X:"
	//TODO: update comments in rename.cc, renamer.cc, renamer.h
	//						squash.cc, pipeline.h, pipeline.cc, etc

	/////////////////////////////////////////////////////////////////////
	// Structure 1: Rename Map Table
	// Entry contains: physical register mapping
	/////////////////////////////////////////////////////////////////////
    vector<uint64_t> RMT;

	/////////////////////////////////////////////////////////////////////
	// Structure 2: Checkpoint Buffer
	// Entry contains: Copy of RMT, PRF Unmapped Bits & Usage Counters, and
	// 					other counters.
	/////////////////////////////////////////////////////////////////////
	typedef struct CPBuff_e{
    	vector<uint64_t> RMT_copy;
		vector<bool> PRFUnnmappedBits_copy;  

		uint64_t uncompleted_instr_count;
		uint64_t load_count;
		uint64_t store_count;
		uint64_t branch_count;
		bool has_amo_instr;
		bool has_csr_instr;
		bool has_except_instr;
	}CPBuff_entry_t;

	typedef struct CPBuffer{
		vector<CPBuff_entry_t> CPBuffEntries;
        uint64_t head;
        uint64_t tail;
        bool head_pb;               //head phase bit
        bool tail_pb;               //tail phase bit
	}CPBuffer_t;

	CPBuffer_t CPBuffer;

	/////////////////////////////////////////////////////////////////////
	// Structure 3: Free List
	//
	// Entry contains: physical register number
	//
	// Notes:
	// * Structure includes head, tail, and their phase bits.
	/////////////////////////////////////////////////////////////////////
	//TODO: update comment
    vector<uint64_t> FL;

	/////////////////////////////////////////////////////////////////////
	// Structure 4: Physical Register File
	// Entry contains: value
	//
	// Notes:
	// * The value must be of the following type: uint64_t
	//   (#include <inttypes.h>, already at top of this file)
	/////////////////////////////////////////////////////////////////////
    vector<uint64_t> PRF; 

	/////////////////////////////////////////////////////////////////////
	// Structure 5: Physical Register File Ready Bit Array
	// Entry contains: ready bit
	/////////////////////////////////////////////////////////////////////
	vector<bool> PRF_rb; 

	/////////////////////////////////////////////////////////////////////
	// Structure 6: PRF Usage Counter
	// Entry contains: the current usage count of physical register. 
	// 
	// Note: Users are src operands of live instrs, checkpointed RMTs, 
	//		(not the current RMT)
	/////////////////////////////////////////////////////////////////////
	vector<uint64_t> PRFUsageCounter;

	/////////////////////////////////////////////////////////////////////
	// Structure 7: PRF Unmapped Bits
	// Entry contains: the current mapping state of physical registers
	//
	// Note: A phys reg gets mapped at renaming when a dest regs gets assigned an 
	// 		 operation. Gets unmmaped when a new phys reg gets assigned that same 
	//		 dest log reg
	/////////////////////////////////////////////////////////////////////
    vector<bool> PRFUnnmappedBits;		

	/////////////////////////////////////////////////////////////////////
	// Private functions.
	// e.g., a generic function to copy state from one map to another.
	/////////////////////////////////////////////////////////////////////

public:
	////////////////////////////////////////
	// Public functions.
	////////////////////////////////////////

	/////////////////////////////////////////////////////////////////////
	// This is the constructor function.
	// When a renamer object is instantiated, the caller indicates:
	// 1. The number of logical registers (e.g., 32).
	// 2. The number of physical registers (e.g., 128).
	// 3. The maximum number of unresolved branches.
	//    Requirement: 1 <= n_branches <= 64.
	// 4. The maximum number of active instructions (Active List size).
	//
	// Tips:
	//
	// Assert the number of physical registers > number logical registers.
	// Assert 1 <= n_branches <= 64.
	// Assert n_active > 0.
	// Then, allocate space for the primary data structures.
	// Then, initialize the data structures based on the knowledge
	// that the pipeline is intially empty (no in-flight instructions yet).
	/////////////////////////////////////////////////////////////////////
	renamer(uint64_t n_log_regs,
		uint64_t n_phys_regs,
		uint64_t n_branches,
		uint64_t n_active);

	/////////////////////////////////////////////////////////////////////
	// This is the destructor, used to clean up memory space and
	// other things when simulation is done.
	// I typically don't use a destructor; you have the option to keep
	// this function empty.
	/////////////////////////////////////////////////////////////////////
	~renamer();


	//////////////////////////////////////////
	// Functions related to Rename Stage.   //
	//////////////////////////////////////////

	/////////////////////////////////////////////////////////////////////
	// The Rename Stage must stall if there aren't enough free physical
	// registers available for renaming all logical destination registers
	// in the current rename bundle.
	//
	// Inputs:
	// 1. bundle_dst: number of logical destination registers in
	//    current rename bundle
	//
	// Return value:
	// Return "true" (stall) if there aren't enough free physical
	// registers to allocate to all of the logical destination registers
	// in the current rename bundle.
	/////////////////////////////////////////////////////////////////////
	bool stall_reg(uint64_t bundle_dst);

	/////////////////////////////////////////////////////////////////////
	// The Rename Stage must stall if there aren't enough free
	// checkpoints for all branches in the current rename bundle.
	//
	// Inputs:
	// 1. bundle_branch: number of branches in current rename bundle
	//
	// Return value:
	// Return "true" (stall) if there aren't enough free checkpoints
	// for all branches in the current rename bundle.
	/////////////////////////////////////////////////////////////////////
	//bool stall_branch(uint64_t bundle_branch); //TODO: no longer need

	/////////////////////////////////////////////////////////////////////
	// The Rename Stage must stall if there aren't enough 
	// checkpoints for offending instr in the current rename bundle.
	//
	// Inputs:
	// 1. bundle_chkpts: number of offending instr in current rename bundle
	//
	// Return value:
	// Return "true" (stall) if there aren't enough free checkpoints
	/////////////////////////////////////////////////////////////////////
	bool stall_checkpoint(uint64_t bundle_chkpts);

	/////////////////////////////////////////////////////////////////////
	// This function is used to get the checkpoint ID affiliated with an instruction.
	/////////////////////////////////////////////////////////////////////
	uint64_t get_checkpoint_ID(bool load, bool store, 
                               bool branch, bool amo, 
							   bool csr);

	/////////////////////////////////////////////////////////////////////
	// This function is used to rename a single source register.
	//
	// Inputs:
	// 1. log_reg: the logical register to rename
	//
	// Return value: physical register name
	/////////////////////////////////////////////////////////////////////
	uint64_t rename_rsrc(uint64_t log_reg);

	/////////////////////////////////////////////////////////////////////
	// This function is used to rename a single destination register.
	//
	// Inputs:
	// 1. log_reg: the logical register to rename
	//
	// Return value: physical register name
	/////////////////////////////////////////////////////////////////////
	uint64_t rename_rdst(uint64_t log_reg);

	/////////////////////////////////////////////////////////////////////
	// This function creates a new branch checkpoint.
	//
	// Inputs: none.
	//
	// Tips:
	//
	// Allocating resources for the branch (a GBM bit and a checkpoint):
	// * Find a free bit -- i.e., a '0' bit -- in the GBM. Assert that
	//   a free bit exists: it is the user's responsibility to avoid
	//   a structural hazard by calling stall_branch() in advance.
	// * Set the bit to '1' since it is now in use by the new branch.
	// * The position of this bit in the GBM is the branch's ID.
	// * Use the branch checkpoint that corresponds to this bit.
	// 
	// The branch checkpoint should contain the following:
	// 1. Shadow Map Table (checkpointed Rename Map Table)
	// 2. checkpointed Free List head pointer and its phase bit
	// 3. checkpointed GBM
	/////////////////////////////////////////////////////////////////////
	//TODO: update comment
	void checkpoint();

	void free_checkpoint();

	/////////////////////////////////////////////////////////////////////
	// Test the ready bit of the indicated physical register.
	// Returns 'true' if ready.
	/////////////////////////////////////////////////////////////////////
	bool is_ready(uint64_t phys_reg);

	/////////////////////////////////////////////////////////////////////
	// Clear the ready bit of the indicated physical register.
	/////////////////////////////////////////////////////////////////////
	void clear_ready(uint64_t phys_reg);


	//////////////////////////////////////////
	// Functions related to the Reg. Read   //
	// and Execute Stages.                  //
	//////////////////////////////////////////

	/////////////////////////////////////////////////////////////////////
	// Return the contents (value) of the indicated physical register.
	/////////////////////////////////////////////////////////////////////
	uint64_t read(uint64_t phys_reg);

	/////////////////////////////////////////////////////////////////////
	// Set the ready bit of the indicated physical register.
	/////////////////////////////////////////////////////////////////////
	void set_ready(uint64_t phys_reg);


	//////////////////////////////////////////
	// Functions related to Writeback Stage.//
	//////////////////////////////////////////

	/////////////////////////////////////////////////////////////////////
	// Write a value into the indicated physical register.
	/////////////////////////////////////////////////////////////////////
	void write(uint64_t phys_reg, uint64_t value);

	/////////////////////////////////////////////////////////////////////
	// Decrement the uncompleted_instr counter in the corresponding checkpoint
	/////////////////////////////////////////////////////////////////////
	void set_complete(uint64_t checkpoint_ID);

	/////////////////////////////////////////////////////////////////////
	// This function is for handling branch resolution.
	//
	// Inputs:
	// 1. AL_index: Index of the branch in the Active List.
	// 2. branch_ID: This uniquely identifies the branch and the
	//    checkpoint in question.  It was originally provided
	//    by the checkpoint function.
	// 3. correct: 'true' indicates the branch was correctly
	//    predicted, 'false' indicates it was mispredicted
	//    and recovery is required.
	//
	// Outputs: none.
	//
	// Tips:
	//
	// While recovery is not needed in the case of a correct branch,
	// some actions are still required with respect to the GBM and
	// all checkpointed GBMs:
	// * Remember to clear the branch's bit in the GBM.
	// * Remember to clear the branch's bit in all checkpointed GBMs.
	//
	// In the case of a misprediction:
	// * Restore the GBM from the branch's checkpoint. Also make sure the
	//   mispredicted branch's bit is cleared in the restored GBM,
	//   since it is now resolved and its bit and checkpoint are freed.
	// * You don't have to worry about explicitly freeing the GBM bits
	//   and checkpoints of branches that are after the mispredicted
	//   branch in program order. The mere act of restoring the GBM
	//   from the checkpoint achieves this feat.
	// * Restore the RMT using the branch's checkpoint.
	// * Restore the Free List head pointer and its phase bit,
	//   using the branch's checkpoint.
	// * Restore the Active List tail pointer and its phase bit
	//   corresponding to the entry after the branch's entry.
	//   Hints:
	//   You can infer the restored tail pointer from the branch's
	//   AL_index. You can infer the restored phase bit, using
	//   the phase bit of the Active List head pointer, where
	//   the restored Active List tail pointer is with respect to
	//   the Active List head pointer, and the knowledge that the
	//   Active List can't be empty at this moment (because the
	//   mispredicted branch is still in the Active List).
	// * Do NOT set the branch misprediction bit in the Active List.
	//   (Doing so would cause a second, full squash when the branch
	//   reaches the head of the Active List. We don’t want or need
	//   that because we immediately recover within this function.)
	/////////////////////////////////////////////////////////////////////
	//TODO: update comment
	uint64_t rollback(uint64_t chkpt_id, bool next, uint64_t &total_loads,
				  	  uint64_t &total_stores, uint64_t &total_branches);

	//////////////////////////////////////////
	// Functions related to Retire Stage.   //
	//////////////////////////////////////////

	///////////////////////////////////////////////////////////////////
	// This function allows the caller to examine the instruction at the head
	// of the Active List.
	//
	// Input arguments: none.
	//
	// Return value:
	// * Return "true" if there exists a checkpoint after the oldest checkpoint and
	// 	 if all instructions between them have completed  
	// * Return "false" otherwise.
	//
	// Output arguments:
	// Simply return the following contents of the head entry of
	// the Active List.  These are don't-cares if the Active List
	// is empty (you may either return the contents of the head
	// entry anyway, or not set these at all).
	// * completed bit
	// * exception bit
	// * load violation bit
	// * branch misprediction bit
	// * value misprediction bit
	// * load flag (indicates whether or not the instr. is a load)
	// * store flag (indicates whether or not the instr. is a store)
	// * branch flag (indicates whether or not the instr. is a branch)
	// * amo flag (whether or not instr. is an atomic memory operation)
	// * csr flag (whether or not instr. is a system instruction)
	// * program counter of the instruction
	/////////////////////////////////////////////////////////////////////
	//TODO: update comment
	bool precommit(uint64_t &chkpt_id, uint64_t &num_loads, uint64_t &num_stores,
                   uint64_t &num_branches, bool &amo, bool &csr, bool &exception);

	/////////////////////////////////////////////////////////////////////
	// This function commits the instruction at the head of the Active List.
	//
	// Tip (optional but helps catch bugs):
	// Before committing the head instruction, assert that it is valid to
	// do so (use assert() from standard library). Specifically, assert
	// that all of the following are true:
	// - there is a head instruction (the active list isn't empty)
	// - the head instruction is completed
	// - the head instruction is not marked as an exception
	// - the head instruction is not marked as a load violation
	// It is the caller's (pipeline's) duty to ensure that it is valid
	// to commit the head instruction BEFORE calling this function
	// (by examining the flags returned by "precommit()" above).
	// This is why you should assert() that it is valid to commit the
	// head instruction and otherwise cause the simulator to exit.
	/////////////////////////////////////////////////////////////////////
	//TODO: update comment
	void commit(uint64_t log_reg);

	//////////////////////////////////////////////////////////////////////
	// Squash the renamer class.
	//
	// Squash all instructions in the Active List and think about which
	// sructures in your renamer class need to be restored, and how.
	//
	// After this function is called, the renamer should be rolled-back
	// to the committed state of the machine and all renamer state
	// should be consistent with an empty pipeline.
	/////////////////////////////////////////////////////////////////////
	void squash();

	//////////////////////////////////////////
	// Functions not tied to specific stage.//
	// CPR support							//
	//////////////////////////////////////////
	void inc_usage_counter(uint64_t phys_reg);
	void dec_usage_counter(uint64_t phys_reg);
	void map(uint64_t phys_reg);
	void unmap(uint64_t phys_reg);
	void try_reg_reclamation(uint64_t phys_reg);
	void set_exception(uint64_t chckpnt_ID);
	bool get_exception(uint64_t chckpnt_ID);

	void set_load_violation(uint64_t chckpnt_ID);
	//TODO: CPR delete
	//void set_branch_misprediction(uint64_t AL_index);
	//void set_value_misprediction(uint64_t AL_index);
};

