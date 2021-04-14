
#include <cstdio>
#include <cinttypes>
#include <iostream>
using namespace std;

#include <vector>

#include "procsim.h"

uint64_t GLOBAL_CLOCK = 0;
bool RAISE_EXCEPTION = false;
bool ALL_DONE = false;
bool freePreg = false;

// You may define any global variables here

bool GLOBAL_STALL = false;

uint64_t J_latency = 1;
uint64_t K_latency = 3;
uint64_t L_latency = 2;
uint64_t store_latency = 1;

uint64_t tag = 0;

uint64_t ROB_size;
uint64_t RS_max_value;
uint64_t F;
uint64_t J;
uint64_t K;
uint64_t L;
uint64_t P;

// uint64_t cycle = 0;

//defining structure for the scheduling queue
struct DisQ
{
    uint64_t address;
    int32_t opcode_sq;
    int32_t dest_reg;
    int32_t src1_reg;
    int32_t src2_reg;
    uint64_t tag;
    uint64_t ld_st_addr;
    uint64_t br_target;
    bool br_taken;
};

struct SQ
{
    uint64_t address;
    int32_t opcode_sq;
    int32_t dest_reg;
    int32_t src1_reg;
    bool src1_ready = false;
    int32_t src2_reg;
    bool src2_ready = false;
    uint64_t tag;
};

// defining structure for the ROB
struct ROB
{
    uint64_t tag;
    uint64_t regno;
    int32_t prev_preg;
    // uint64_t exception_bit;
    bool ready;
};

//defining structure for the ROB
struct Reg
{
    int32_t preg;
    uint64_t ready;
    bool free;
};

struct FU
{
    uint64_t tag;
    int32_t dest_reg;
    int latency;
};

//Creating tables for SQ, ROB, Register File
vector<SQ> RS;   //Reservation station
vector<DisQ> DQ; //Dispatch Queue
vector<ROB> ROB_T;
vector<Reg> RegF;
vector<int32_t> RAT;

vector<FU> Adder;
vector<FU> Multiplier;
vector<FU> Store_Loader;

// We suggest using std::deque for the dispatch queue since instructions are both added and removed
// from it. Using an std::vector is really inefficient for popping from the front

/**
 * Subroutine for initializing the processor. You many add and initialize any global or heap
 * variables as needed.
 *
 * @param conf Pointer to the configuration. Read only in this function
 */
void setup_proc(const procsim_conf *conf)
{
    GLOBAL_CLOCK = 0;
    RAISE_EXCEPTION = false;
    ALL_DONE = false;

    // Student code should go below this line
    J = conf->F;
    J = conf->J;
    K = conf->K;
    L = conf->L;
    P = conf->P;

    ROB_size = conf->R;
    RS_max_value = 2 * (J + K + L);

    // Note - The schedule queue can hold a maximum of 2 * (J + K + L) instructions
    Reg regObject;
    for (int32_t i = 0; (uint64_t)i < P; i++)
    {
        if (i < 32)
        {
            regObject.preg = i;
            regObject.ready = 1;
            regObject.free = false;
            RegF.push_back(regObject);

            RAT.push_back(i);
        }
        else
        {
            regObject.preg = i;
            regObject.ready = 1;
            regObject.free = true;
            RegF.push_back(regObject);
        }
    }
}

// Write helper methods for each superscalar operation here

//////////////////////////////////////////////////////////////////////////////////
////////////////////////////// STUDENT FUNCTION //////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////

//Free PReg function
uint64_t freePRegIndex()
{
    freePreg = false;
    for (uint64_t i = 32; i < P; i++)
    {
        if (RegF[i].free)
        {
            freePreg = true;
            return i;
            break;
        }
    }
}

void freeRob(int32_t src_dq, int32_t src_sq, bool ready)
{
    for (uint64_t i = ROB_T.size(); i > 0; i--)
    {
        if (ROB_T[i].regno == src_dq)
        {
            if (ROB_T[i].ready)
            {
                ready = true;
                src_sq = RAT[src_dq];
            }
            else
            {
                src_sq = ROB_T[i].prev_preg;
                ready = false;
            }
            break;
        }
        else
        {
            src_sq = RAT[src_dq];
            ready = true;
        }
    }
}

static void fetch(procsim_stats *stats)
{
    // Sample for how instructions are read from the trace
    instruction *new_inst = new instruction;
    read_instruction(new_inst);

    // GLOBAL_CLOCK++;

    for (uint64_t i = 0; i < F; i++)
    {
        if (new_inst->opcode == 5)
        {
            DQ.push_back({new_inst->inst_addr,
                          new_inst->opcode,
                          new_inst->dest_reg,
                          new_inst->src_reg[0],
                          new_inst->src_reg[1],
                          tag + 1,
                          new_inst->ld_st_addr});
        }
        else if (new_inst->opcode == 6)
        {
            DisQ dqObject;
            dqObject.address = new_inst->inst_addr;
            dqObject.opcode_sq = new_inst->opcode;
            dqObject.dest_reg = new_inst->dest_reg;
            dqObject.src1_reg = new_inst->src_reg[0];
            dqObject.src2_reg = new_inst->src_reg[1];
            dqObject.br_target = new_inst->br_target;
            dqObject.br_taken = new_inst->br_taken;
            dqObject.tag = tag + 1;
            DQ.push_back(dqObject);
        }
        else
        {
            DQ.push_back({new_inst->inst_addr, new_inst->opcode, new_inst->dest_reg, new_inst->src_reg[0], new_inst->src_reg[1], tag + 1});
        }

        new_inst->fetch_cycle = GLOBAL_CLOCK;
    }
}

static void dispatch(procsim_stats *stats)
{

    while (!DQ.empty() && RS.size() <= RS_max_value && ROB_T.size() < ROB_size)
    {
        SQ sqObject;
        if (DQ[0].opcode_sq == OP_NOP)
        {
            continue;
        }
        else if (DQ[0].dest_reg < (0))
        {
            sqObject.address = DQ[0].address;
            sqObject.opcode_sq = DQ[0].opcode_sq;
            sqObject.dest_reg = -1;
            sqObject.tag = DQ[0].tag;
            if (DQ[0].src1_reg < 0)
            {
                sqObject.src1_reg = -1;
            }
            else
            {
                // sqObject.src1_reg = RAT[DQ[0].src1_reg];
                freeRob(DQ[0].src1_reg, sqObject.src1_reg, sqObject.src1_ready);
            }
            if (DQ[0].src2_reg < 0)
            {
                sqObject.src2_reg = -1;
            }
            else
            {
                freeRob(DQ[0].src2_reg, sqObject.src2_reg, sqObject.src2_ready);
            }

            // DQ.erase(DQ.begin());
        }
        else
        {
            if (freePreg)
            {
                sqObject.address = DQ[0].address;
                sqObject.opcode_sq = DQ[0].opcode_sq;
                sqObject.tag = DQ[0].tag;
                sqObject.dest_reg = RegF[freePRegIndex()].preg;
                RegF[freePRegIndex()].free = false;
                RAT[DQ[0].dest_reg] = sqObject.dest_reg;
                if (DQ[0].src1_reg < 0)
                {
                    sqObject.src1_reg = -1;
                }
                else
                {
                    freeRob(DQ[0].src1_reg, sqObject.src1_reg, sqObject.src1_ready);
                }
                if (DQ[0].src2_reg < 0)
                {
                    sqObject.src2_reg = -1;
                }
                else
                {
                    freeRob(DQ[0].src2_reg, sqObject.src2_reg, sqObject.src2_ready);
                }
            }
        }

        RS.push_back(sqObject);

        ROB robObject;
        robObject.prev_preg = RAT[DQ[0].dest_reg];
        robObject.regno = DQ[0].dest_reg;
        robObject.ready = false;
        robObject.tag = DQ[0].tag;
        ROB_T.push_back(robObject);

        DQ.erase(DQ.begin());
    }
}
// }

static void schedule(procsim_stats *stats)
{
    // for (uint64_t i = 0; i < RS.size(); i++)
    // {
    //     if ((RS[i].opcode_sq == OP_ADD || RS[i].opcode_sq == OP_BR) && J > 0)
    //     {
    //         J--;
    //         if (RS[i].src1_ready && RS[i].src2_ready)
    //         {
    //             J_latency--;
    //         }
    //     }
    //     if (RS[i].opcode_sq == OP_MUL && K > 0)
    //     {
    //         J--;
    //         if (RS[i].src1_ready && RS[i].src2_ready)
    //         {
    //             K_latency--;
    //         }
    //     }
    //     if (RS[i].opcode_sq == OP_MUL && K > 0)
    //     {
    //         J--;
    //         if (RS[i].src1_ready && RS[i].src2_ready)
    //         {
    //             K_latency--;
    //         }
    //     }
    // }

    for (uint64_t i = 0; i < RS.size(); i++)
    {
        if (RS[i].src1_ready && RS[i].src2_ready)
        {
            if ((RS[i].opcode_sq == OP_ADD || RS[i].opcode_sq == OP_BR) && J > 0)
            {
                FU fuObject;
                fuObject.dest_reg = RS[i].dest_reg;
                fuObject.latency = 1;
                Adder.push_back(fuObject);
                J - 1;
            }
            else if (RS[i].opcode_sq == OP_MUL && K > 0)
            {
                FU fuObject;
                fuObject.dest_reg = RS[i].dest_reg;
                fuObject.latency = 3;
                Multiplier.push_back(fuObject);
                K - 1;
            }
            else if (RS[i].opcode_sq == OP_LOAD && L > 0)
            {
                FU fuObject;
                fuObject.dest_reg = RS[i].dest_reg;
                fuObject.latency = 2;
                Store_Loader.push_back(fuObject);
                L - 1;
            }
            else
            {
                FU fuObject;
                fuObject.dest_reg = RS[i].dest_reg;
                fuObject.latency = 1;
                Store_Loader.push_back(fuObject);
            }
        }
    }
}

static void execute(procsim_stats *stats)
{
}

static void state_update(procsim_stats *stats)
{
}

/**
 * Subroutine that simulates the processor. The processor should fetch instructions as
 * appropriate, until all instructions have executed
 *
 * @param stats Pointer to the statistics structure
 * @param conf Pointer to the configuration. Read only in this function
 */
void run_proc(procsim_stats *stats, const procsim_conf *conf)
{
    do
    {
        state_update(stats);
        execute(stats);
        schedule(stats);
        dispatch(stats);
        fetch(stats);

        GLOBAL_CLOCK++; // Clock the processor

        // Raise an exception/interrupt every 'I' clock cycles
        // When the RAISE_EXCEPTION FLAG is raised -- an Interrupt instruction is added
        // to the front of the dispatch queue while the schedule queue and ROB are flushed
        // Execution resumes starting from the Interrupt Instruction. The flushed instructions
        // are re-executed
        if ((GLOBAL_CLOCK % conf->I) == 0)
        {
            RAISE_EXCEPTION = true;
        }

        // Run the loop until all the instructions in the trace have retired
        // Feel free to replace the condition of the do-while loop
    } while (!ALL_DONE);
}

/**
 * Subroutine for cleaning up any outstanding instructions and calculating overall statistics
 * such as average IPC, average fire rate etc.
 *
 * @param stats Pointer to the statistics structure
 */
void complete_proc(procsim_stats *stats)
{
}

// empty sllot, empty in rob, free physical registern

//
