// pipeline.cpp
// Implements functions to simulate a pipelined processor.
// Adithi S Upadhya

#include "pipeline.h"
#include <cstdlib>
#include <stdio.h>
#include <unistd.h>

/**
 * To read a single trace record from the trace file and use it to populate the
 * given fetch_op.
 * 
 * @param p the pipeline whose trace file should be read
 * @param fetch_op the PipelineLatch struct to populate
 */
void pipe_get_fetch_op(Pipeline *p, PipelineLatch *fetch_op)
{
    TraceRec *trace_rec = &fetch_op->trace_rec;
    uint8_t *trace_rec_buf = (uint8_t *)trace_rec;
    size_t bytes_read_total = 0;
    ssize_t bytes_read_last = 0;
    size_t bytes_left = sizeof(*trace_rec);

    // Read a total of sizeof(TraceRec) bytes from the trace file.
    while (bytes_left > 0)
    {
        bytes_read_last = read(p->trace_fd, trace_rec_buf, bytes_left);
        if (bytes_read_last <= 0)
        {
            // EOF or error
            break;
        }

        trace_rec_buf += bytes_read_last;
        bytes_read_total += bytes_read_last;
        bytes_left -= bytes_read_last;
    }

    // Check for error conditions.
    if (bytes_left > 0 || trace_rec->op_type >= NUM_OP_TYPES)
    {
        fetch_op->valid = false;
        p->halt_op_id = p->last_op_id;

        if (p->last_op_id == 0)
        {
            p->halt = true;
        }

        if (bytes_read_last == -1)
        {
            fprintf(stderr, "\n");
            perror("Couldn't read from pipe");
            return;
        }

        if (bytes_read_total == 0)
        {
            // No more trace records to read
            return;
        }

        // Too few bytes read or invalid op_type
        fprintf(stderr, "\n");
        fprintf(stderr, "Error: Invalid trace file\n");
        return;
    }

    // Got a valid trace record!
    fetch_op->valid = true;
    fetch_op->stall = false;
    fetch_op->is_mispred_cbr = false;
    fetch_op->op_id = ++p->last_op_id;
}

/**
 * To allocate and initialize a new pipeline.
 * 
 * @param trace_fd the file descriptor from which to read trace records
 * @return a pointer to a newly allocated pipeline
 */
Pipeline *pipe_init(int trace_fd)
{
    printf("\n** PIPELINE IS %d WIDE **\n\n", PIPE_WIDTH);

    // Allocate pipeline.
    Pipeline *p = (Pipeline *)calloc(1, sizeof(Pipeline));

    // Initialize pipeline.
    p->trace_fd = trace_fd;
    p->halt_op_id = (uint64_t)(-1) - 3;

    // Allocate and initialize a branch predictor if needed.
    if (BPRED_POLICY != BPRED_PERFECT)
    {
        p->b_pred = new BPred(BPRED_POLICY);
    }

    return p;
}

/**
 * To print out the state of the pipeline latches for debugging purposes.
 * 
 * @param p the pipeline
 */
void pipe_print_state(Pipeline *p)
{
    printf("\n--------------------------------------------\n");
    printf("Cycle count: %lu, retired instructions: %lu\n",
           (unsigned long)p->stat_num_cycle,
           (unsigned long)p->stat_retired_inst);

    // Print table header
    for (uint8_t latch_type = 0; latch_type < NUM_LATCH_TYPES; latch_type++)
    {
        switch (latch_type)
        {
        case IF_LATCH:
            printf(" IF:    ");
            break;
        case ID_LATCH:
            printf(" ID:    ");
            break;
        case EX_LATCH:
            printf(" EX:    ");
            break;
        case MA_LATCH:
            printf(" MA:    ");
            break;
        default:
            printf(" ------ ");
        }
    }
    printf("\n");

    // Print row for each lane in pipeline width
    for (uint8_t i = 0; i < PIPE_WIDTH; i++)
    {
        for (uint8_t latch_type = 0; latch_type < NUM_LATCH_TYPES;
             latch_type++)
        {
            if (p->pipe_latch[latch_type][i].valid)
            {
                printf(" %6lu ",
                       (unsigned long)p->pipe_latch[latch_type][i].op_id);
            }
            else
            {
                printf(" ------ ");
            }
        }
        printf("\n");
    }
    for (uint8_t i = 0; i < PIPE_WIDTH; i++)
    {
        for (uint8_t latch_type = 0; latch_type < NUM_LATCH_TYPES;
             latch_type++)
        {
            if (p->pipe_latch[latch_type][i].valid)
            {
                int dest = (p->pipe_latch[latch_type][i].trace_rec.dest_needed) ? 
                        p->pipe_latch[latch_type][i].trace_rec.dest_reg : -1;
                int src1 = (p->pipe_latch[latch_type][i].trace_rec.src1_needed) ? 
                        p->pipe_latch[latch_type][i].trace_rec.src1_reg : -1;
                int src2 = (p->pipe_latch[latch_type][i].trace_rec.src2_needed) ? 
                        p->pipe_latch[latch_type][i].trace_rec.src2_reg : -1;
                int cc_read = p->pipe_latch[latch_type][i].trace_rec.cc_read;
                int cc_write = p->pipe_latch[latch_type][i].trace_rec.cc_write;
                const char *op_type;
                if (p->pipe_latch[latch_type][i].trace_rec.op_type == OP_ALU)
                    op_type = "ALU";
                else if (p->pipe_latch[latch_type][i].trace_rec.op_type == OP_LD)
                    op_type = "LD";
                else if (p->pipe_latch[latch_type][i].trace_rec.op_type == OP_ST)
                    op_type = "ST";
                else if (p->pipe_latch[latch_type][i].trace_rec.op_type == OP_CBR)
                    op_type = "BR";
                else
                    op_type = "OTHER";

                printf("(%lu : %s) dest: %d, src1: %d, src2: %d , ccread: %d, ccwrite: %d\n",
                       (unsigned long)p->pipe_latch[latch_type][i].op_id,
                       op_type,
                       dest,
                       src1,
                       src2,
                       cc_read,
                       cc_write);
            }
            else
            {
                printf(" ------ \n");
            }
        }
        printf("\n");
    }
}

/**
 * To simulate one cycle of all stages of a pipeline.
 * 
 * @param p the pipeline to simulate
 */
void pipe_cycle(Pipeline *p)
{
    p->stat_num_cycle++;

    // In hardware, all pipeline stages execute in parallel, and each pipeline
    // latch is populated at the start of the next clock cycle.

    // In our simulator, we simulate the pipeline stages one at a time in
    // reverse order, from the Write Back stage (WB) to the Fetch stage (IF).
    // We do this so that each stage can read from the latch before it and
    // write to the latch after it without needing to "double-buffer" the
    // latches.

    // Additionally, it means that earlier pipeline stages can know about
    // stalls triggered in later pipeline stages in the same cycle, as would be
    // the case with hardware stall signals asserted by combinational logic.

    pipe_cycle_WB(p);
    pipe_cycle_MA(p);
    pipe_cycle_EX(p);
    pipe_cycle_ID(p);
    pipe_cycle_IF(p);

    #ifdef DEBUG
    pipe_print_state(p);
    #endif
}

/**
 * To simulate one cycle of the Write Back stage (WB) of a pipeline.
 * 
 * @param p the pipeline to simulate
 */
void pipe_cycle_WB(Pipeline *p)
{
    for (unsigned int i = 0; i < PIPE_WIDTH; i++)
    {
        if (!p->pipe_latch[MA_LATCH][i].valid) continue;

        p->stat_retired_inst++;

        if (p->pipe_latch[MA_LATCH][i].op_id >= p->halt_op_id)
        {
            // Halt the pipeline if we've reached the end of the trace.
            p->halt = true;
        }

        if (p->pipe_latch[MA_LATCH][i].is_mispred_cbr)
            p->fetch_cbr_stall = false;  //unset till cycle after mispred branch is in MA 
    }
}

/**
 * To simulate one cycle of the Memory Access stage (MA) of a pipeline.
 * 
 * @param p the pipeline to simulate
 */
void pipe_cycle_MA(Pipeline *p)
{
    for (unsigned int i = 0; i < PIPE_WIDTH; i++)
    {
        // Copy each instruction from the EX latch to the MA latch.
        p->pipe_latch[MA_LATCH][i] = p->pipe_latch[EX_LATCH][i];
    }
}

/**
 * To simulate one cycle of the Execute stage (EX) of a pipeline.
 * 
 * @param p the pipeline to simulate
 */
void pipe_cycle_EX(Pipeline *p)
{
    for (unsigned int i = 0; i < PIPE_WIDTH; i++)
    {
        // Copy each instruction from the ID latch to the EX latch.
        p->pipe_latch[EX_LATCH][i] = p->pipe_latch[ID_LATCH][i];

        /*
        ============================{ A1 }============================
        I have chosen to only set the stall bit of ID. As such, for instructions 
        to be NOPs in in this implementation our EX stage checks the stall bit
        set by ID. If stall is set, then we convert it to a NOP. I primarily chose 
        doing it this way because it meant less lines of code.

        The alternative to setting both the valid bit and the stall bit does work, but
        I personally don't like it since 1) instead of one line of code each time you
        need to stall it's two and 2) the output would show that you have a NOP in
        the ID latch which doesn't make sense since ID can only get stalled in this
        portion of the simulation, not squashed.
        */        
        if (p->pipe_latch[EX_LATCH][i].stall) {
            p->pipe_latch[EX_LATCH][i].valid = 0;
        }
    }
}

/**
 * To simulate one cycle of the Instruction Decode stage (ID) of a pipeline.
 * 
 * @param p the pipeline to simulate
 */
void pipe_cycle_ID(Pipeline *p)
{
    /*
    ============================{ A1 }============================
    We have to unconditionally move over all pipeline stages before
    we do any stall logic checking. Consider this case...
    IF:   ID:   EX:   MA:
    7     5     3     1
    8     6     4     2

    On the next cycle, when we reach this code, it's entirely possible
    to have this configuration:
    IF:   ID:   EX:   MA:
    7     7     5     3
    8     6     4     2    
    
    This is incorrect since we havn't stalled yet, so 7 & 8 should always
    be together.
    */  
    for (unsigned int i = 0; i < PIPE_WIDTH; i++) {
        // Copy each instruction from the IF latch to the ID latch.
        p->pipe_latch[ID_LATCH][i] = p->pipe_latch[IF_LATCH][i];
    }

    /*
    ============================{ A1 }============================
    Iterate through each of our pipelines and check for RAW hazards
    */  
    for (unsigned int i = 0; i < PIPE_WIDTH; i++) {
        /*
        ============================{ A3 }============================
        Create index trackers for oldest dependency of each type.

        Initialize to PIPE_WIDTH since no index should ever equal our 
        superscalar width. If we never change from PIPE_WIDTH, there were no 
        dependencies of that type on pipeline i's ID_LATCH in the EX phase.
        */  
        unsigned int oldest_src1_j = PIPE_WIDTH;
        unsigned int oldest_src2_j = PIPE_WIDTH;
        unsigned int oldest_cc_j = PIPE_WIDTH;

        /*
        ============================{ A2 }============================
        Inner-for loop to iterate through each of our pipelines again.
        Allows you to check for, as an example, RAW hazards between the ID
        stage of pipeline 0 and the EX stage of pipeline 1.
        */ 
        for (unsigned int j = 0; j < PIPE_WIDTH; j++) {
            if (ENABLE_MEM_FWD) {
                /*
                ============================{ A3 }============================
                You can always forward from the ME stage, so if forwarding is
                enabled then no need to check the MA_LATCH
                */  
            } else {
                /*
                ============================{ A1 }============================
                Check for RAW hazards in the MA latch. Generalize to a function
                call for nicer-looking code.
                */ 
                if (has_raw_hazards(p, i, j, MA_LATCH)) {
                    p->pipe_latch[ID_LATCH][i].stall = 1;
                }
            }

            if (ENABLE_EXE_FWD) {
                /*
                ============================{ A3 }============================
                We only have a load-use hazard if the oldest (later in program trace) 
                instruction for any of the hazard-causing dependencies (src1, src2, cc)
                is caused by an LD instruction. Therefore, iterate through all the EX 
                stages and track the oldest hazard-causing pipeline index for src1, src2, 
                and cc. If any of these are an LD instruction, then we have a load-use hazard.
                */
                int output = has_raw_hazards(p, i, j, EX_LATCH);

                // Check the bits of the output for each type of dependency
                // See the has_raw_hazards docstring for details
                int cc_dependency = output >> 2 & 1;
                int src2_dependency = output >> 1 & 1;
                int src1_dependency = output & 1;

                // Checking if we can update src1 dependency
                if (src1_dependency && 
                    (oldest_src1_j == PIPE_WIDTH || p->pipe_latch[EX_LATCH][j].op_id > p->pipe_latch[EX_LATCH][oldest_src1_j].op_id)) 
                {
                    oldest_src1_j = j;
                }

                // Checking we can update src2 dependency
                if (src2_dependency && 
                    (oldest_src2_j == PIPE_WIDTH || p->pipe_latch[EX_LATCH][j].op_id > p->pipe_latch[EX_LATCH][oldest_src2_j].op_id)) 
                {
                    oldest_src2_j = j;
                }

                // Checking if we can update cc dependency
                if (cc_dependency &&
                    (oldest_cc_j == PIPE_WIDTH || p->pipe_latch[EX_LATCH][j].op_id > p->pipe_latch[EX_LATCH][oldest_cc_j].op_id)) 
                {
                    oldest_cc_j = j;
                }
            } else {
                /*
                ============================{ A1 }============================
                Check for RAW hazards in the EX latch. Generalize to a function
                call for nicer-looking code.
                */ 
                if (has_raw_hazards(p, i, j, EX_LATCH)) {
                    p->pipe_latch[ID_LATCH][i].stall = 1;
                }
            }
            /*
            ============================{ A2 }============================
            Check for RAW hazards in the ID latch on instructions which 
            come earlier in the program trace. For example...
            IF:   ID:   EX:   MA:
            7     5     3     1
            8     6     4     2 

            6 can depend on 1, 2, 3, or 4, but can also depend on 5.
            5 can depend on 1, 2, 3, or 4, but not 6 because it comes later.

            Therefore, we need to check for RAW hazards in the ID stage ONLY
            if the instruction comes earlier in the program trace.
            */ 
            if (p->pipe_latch[ID_LATCH][i].op_id > p->pipe_latch[ID_LATCH][j].op_id && 
                has_raw_hazards(p, i, j, ID_LATCH)) 
            {
                p->pipe_latch[ID_LATCH][i].stall = 1;
            }
        }
        /*
        ============================{ A3 }============================
        After iterating through EX in each pipeline j to check if there is a RAW dependency
        on pipeline i's ID, check if any of the oldest (latest in the program trace) 
        hazard-causing instructions for each type (src1, src2, cc) are a LD. If any of them 
        are a LD, we have to stall ID.

        Ensure that our indices were actually updated; they were set to PIPE_WIDTH by default.
        */
        if (ENABLE_EXE_FWD) {
            if ((oldest_src1_j != PIPE_WIDTH && p->pipe_latch[EX_LATCH][oldest_src1_j].trace_rec.op_type == OP_LD) ||
                (oldest_src2_j != PIPE_WIDTH && p->pipe_latch[EX_LATCH][oldest_src2_j].trace_rec.op_type == OP_LD) ||
                (oldest_cc_j != PIPE_WIDTH && p->pipe_latch[EX_LATCH][oldest_cc_j].trace_rec.op_type == OP_LD)) {
                p->pipe_latch[ID_LATCH][i].stall = 1;
            }
        }
    }

    /*
    ============================{ A2 }============================
    To maintain the in-order property of the pipeline, if we ever have
    a stalled instruction in ID then we must stall all later instructions
    in the program trace. For instance...
    IF:   ID:   EX:   MA:
    7     5     3     1
    8     6     4     2

    If 5 is stalled here and we allow 6 to move, then we'd get...
    IF:   ID:   EX:   MA:
    7     5     ---   3 
    9     8     6     4 

    This would mean 6 to finishes before 5, which is not allowed. Therefore,
    we should instead also stall 6 regardless of there being a RAW hazard...
    IF:   ID:   EX:   MA:
    7     5     ---   3
    8     6     ---   4
    */ 
    for (unsigned int i = 0; i < PIPE_WIDTH; i++) {
        if (!p->pipe_latch[ID_LATCH][i].stall) continue;

        for (unsigned int j = 0; j < PIPE_WIDTH; j++) {
            if (p->pipe_latch[ID_LATCH][j].op_id > p->pipe_latch[ID_LATCH][i].op_id) {
                p->pipe_latch[ID_LATCH][j].stall = 1;
            }
        }
    }
}

/**
 * Helper method for pipe_cycle_ID.
 * 
 * Using the passed in pipeline, checks the ID_LATCH of i &
 * the latch of j to see if there are any src1, src2, or cc
 * RAW hazards.
 * 
 * Reurns an bit-vector (set as an int) based on the type of hazard (for A3 logic).
 * The bits in the vector are as follows:
 * {31 - 3}: Unused
 * {2}: cc dependency
 * {1}: src2 dependency
 * {0}: src1 dependency
 * 
 * @param p the pipeline to simulate
 * @param i the pipeline index of the ID_LATCH to compare to
 * @param j the pipeline index of the @latch to compare with
 * @param latch the latch of the pipeline to compare with
 * @return bit-vector as an int where the first 3 bits indicate the type of dependency/dependencies
 */
int has_raw_hazards(Pipeline *p, unsigned int i, unsigned int j, LatchType latch) {
    if (!p->pipe_latch[latch][j].valid) return 0;

    int output = 0;

    uint8_t src1_needed = p->pipe_latch[ID_LATCH][i].trace_rec.src1_needed;
    uint8_t src2_needed = p->pipe_latch[ID_LATCH][i].trace_rec.src2_needed;
    uint8_t dest_needed = p->pipe_latch[latch][j].trace_rec.dest_needed;

    uint8_t src1_reg = p->pipe_latch[ID_LATCH][i].trace_rec.src1_reg;
    uint8_t src2_reg = p->pipe_latch[ID_LATCH][i].trace_rec.src2_reg;
    uint8_t dest_reg = p->pipe_latch[latch][j].trace_rec.dest_reg;

    uint8_t cc_read = p->pipe_latch[ID_LATCH][i].trace_rec.cc_read;
    uint8_t cc_write = p->pipe_latch[latch][j].trace_rec.cc_write;

    if (src1_needed && dest_needed && src1_reg == dest_reg) {
        output |= 1;
    }

    if (src2_needed && dest_needed && src2_reg == dest_reg) {
        output |= 2;
    }

    if (cc_read && cc_write) {
        output |= 4;
    }

    return output;
}

/**
 * To simulate one cycle of the Instruction Fetch stage (IF) of a pipeline.
 * 
 * 
 * @param p the pipeline to simulate
 */
void pipe_cycle_IF(Pipeline *p)
{
    PipelineLatch fetch_op;

    for (unsigned int i = 0; i < PIPE_WIDTH; i++)
    {
        if (p->fetch_cbr_stall) //conditional branch set 
        {
            //if both ID stall and mispred are present, stall from ID gets priority
            if (p->pipe_latch[IF_LATCH][i].valid && p->pipe_latch[ID_LATCH][i].stall)
                p->pipe_latch[IF_LATCH][i].valid = true; //no bubble in this case
            else
                p->pipe_latch[IF_LATCH][i].valid = false; //insert bubble whenever new instruction is fetched
        }

        // If ID is stalled, do not fetch the next instruction 
        // TODO: check whether or not to keep this 
        if (p->pipe_latch[ID_LATCH][i].stall) continue;

        //fetch new instruction if there is no stall or cbr misprediction
        if (!p->pipe_latch[ID_LATCH][i].stall && !p->fetch_cbr_stall)
        {
            // Read an instruction from the trace file.
            pipe_get_fetch_op(p, &fetch_op);

            // Handle branch (mis)prediction.
            if (BPRED_POLICY != BPRED_PERFECT)
            {
                pipe_check_bpred(p, &fetch_op);
            }

            // Copy the instruction to the IF latch.
            p->pipe_latch[IF_LATCH][i] = fetch_op;

        }

        // printf("Fetching %lu!\n", p->pipe_latch[IF_LATCH][i].op_id);
    }
}

/**
 * If the instruction just fetched is a conditional branch, check for a branch
 * misprediction, update the branch predictor, and set appropriate flags in the
 * pipeline.
 * 
 * @param p the pipeline
 * @param fetch_op the pipeline latch containing the operation fetched
 */
void pipe_check_bpred(Pipeline *p, PipelineLatch *fetch_op)
{
    
    if (fetch_op->trace_rec.op_type == OP_CBR) //checking for conditional branch
    {
        BranchDirection prediction = p->b_pred->predict(fetch_op->trace_rec.inst_addr); //predict(PC)

        if (prediction != fetch_op->trace_rec.br_dir) //misprediction 
        {
            fetch_op->is_mispred_cbr = true;
            p->fetch_cbr_stall = true;
        }
        p->b_pred->update(fetch_op->trace_rec.inst_addr, prediction, BranchDirection(fetch_op->trace_rec.br_dir));

    }
}
