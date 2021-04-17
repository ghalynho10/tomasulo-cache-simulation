
#include <cstdio>
#include <cinttypes>
#include <iostream>

#include <algorithm>
using namespace std;

#include <vector>

#include "procsim.h"

uint64_t GLOBAL_CLOCK = 0;
bool RAISE_EXCEPTION = false;
bool ALL_DONE = false;
bool freePreg = false;

//debug variables
bool read_instruc;

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

uint64_t Max_K;
uint64_t Max_L;

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
    // uint64_t fetch_cycle;        // cycle in which fetched
    uint64_t ld_st_addr;
    uint64_t br_target;
    bool br_taken;

    // uint64_t dispatch_cycle;     // cycle in which dispatched
    // uint64_t schedule_cycle;     // cycle in which scheduled
    // uint64_t execute_cycle;      // cycle in which executed
    // uint64_t state_update_cycle; // cycle in which retired
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

    // uint64_t fetch_cycle;        // cycle in which fetched
    // uint64_t dispatch_cycle;     // cycle in which dispatched
    // uint64_t schedule_cycle;     // cycle in which scheduled
    // uint64_t execute_cycle;      // cycle in which executed
    // uint64_t state_update_cycle; // cycle in which retired
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
    bool ready;
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
vector<ROB> ROB_T_Copy;
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
    F = conf->F;
    J = conf->J;
    K = conf->K;
    L = conf->L;
    P = conf->P;

    Max_K = conf->K;
    Max_L = conf->L;

    ROB_size = conf->R;
    RS_max_value = 2 * (J + K + L);

    // Note - The schedule queue can hold a maximum of 2 * (J + K + L) instructions
    Reg regObject;
    for (int32_t i = 0; (uint64_t)i < P; i++)
    {
        if (i < 32)
        {
            regObject.preg = i;
            regObject.ready = true;
            regObject.free = false;
            RegF.push_back(regObject);

            RAT.push_back(i);
        }
        else
        {
            regObject.preg = i;
            regObject.ready = true;
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
        if (ROB_T[i].regno == (uint64_t)src_dq)
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
        else if (i == 0)
        {
            src_sq = RAT[src_dq];
            ready = true;
        }
    }
}

bool robSorting(const ROB &a, ROB &b)
{
    return a.tag < b.tag;
}

static void fetch(procsim_stats *stats)
{
    // Sample for how instructions are read from the trace
    instruction *new_inst = new instruction;
    read_instruc = read_instruction(new_inst);

    if (!read_instruc)
    {
        ALL_DONE = true;
    }

    if (RAISE_EXCEPTION)
    {
        DisQ dqObject;
        dqObject.opcode_sq = 7;
        dqObject.tag = tag;
        DQ.insert(DQ.begin(), dqObject);
    }

    for (uint64_t i = 0; i < F; i++)
    {
        tag++;
        if (new_inst->opcode == 5 || new_inst->opcode == 4)
        {
            DisQ dqObject;
            dqObject.address = new_inst->inst_addr;
            dqObject.opcode_sq = new_inst->opcode;
            dqObject.dest_reg = new_inst->dest_reg;
            dqObject.src1_reg = new_inst->src_reg[0];
            dqObject.src2_reg = new_inst->src_reg[1];
            dqObject.tag = tag;
            dqObject.ld_st_addr = new_inst->ld_st_addr;
            DQ.push_back(dqObject);
            // DQ.push_back({new_inst->inst_addr,
            //               new_inst->opcode,
            //               new_inst->dest_reg,
            //               new_inst->src_reg[0],
            //               new_inst->src_reg[1],
            //               tag + 1,
            //               //   new_inst->fetch_cycle,
            //               new_inst->ld_st_addr});
            stats->store_instructions++; // stats for store instructions
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
            dqObject.tag = tag;
            DQ.push_back(dqObject);

            stats->branch_instructions++; //stats for branch instructions
        }
        else
        {
            DisQ dqObject;
            dqObject.address = new_inst->inst_addr;
            dqObject.opcode_sq = new_inst->opcode;
            dqObject.dest_reg = new_inst->dest_reg;
            dqObject.src1_reg = new_inst->src_reg[0];
            dqObject.src2_reg = new_inst->src_reg[1];
            dqObject.tag = tag;
            DQ.push_back(dqObject);
            // DQ.push_back({new_inst->inst_addr, new_inst->opcode, new_inst->dest_reg, new_inst->src_reg[0], new_inst->src_reg[1], tag + 1});
        }

        // new_inst->fetch_cycle = GLOBAL_CLOCK;
    }
}

static void dispatch(procsim_stats *stats)
{

    while (!DQ.empty() && RS.size() <= RS_max_value && ROB_T.size() < ROB_size)
    {
        if (GLOBAL_STALL)
        {
            break;
        }
        if ((DQ[0].opcode_sq != OP_NOP) && DQ[0].opcode_sq != 7)
        {
            if (DQ[0].opcode_sq == OP_LOAD)
            {
                stats->load_instructions++; //stats for load instructions
            }
            SQ sqObject;

            if (DQ[0].dest_reg < (0))
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
            else
            {
                if (freePreg)
                {
                    sqObject.address = DQ[0].address;
                    sqObject.opcode_sq = DQ[0].opcode_sq;
                    sqObject.tag = DQ[0].tag;
                    sqObject.dest_reg = RegF[freePRegIndex()].preg;
                    RegF[freePRegIndex()].free = false;
                    RegF[freePRegIndex()].ready = false;
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
        }
        else if (DQ[0].opcode_sq == 7)
        {
            SQ sqObject;
            sqObject.dest_reg = -1;
            // sqObject.src1_reg = DQ[0].src1_reg;
            sqObject.src1_ready = true;
            // sqObject.src2_reg = DQ[0].src2_reg;
            sqObject.tag = DQ[0].tag;
            sqObject.src2_ready = true;
            RS.push_back(sqObject);

            ROB robObject;
            robObject.prev_preg = -1;
            robObject.regno = -1;
            robObject.ready = false;
            robObject.tag = DQ[0].tag;
            ROB_T.push_back(robObject);

            GLOBAL_STALL = true;

            stats->num_exceptions++; //stats for interrupt instructions
        }
        else
        {
            continue;
        }

        DQ.erase(DQ.begin());
    }
}
// }

static void schedule(procsim_stats *stats)
{

    for (uint64_t i = 0; i < RS.size(); i++)
    {
        if (RS[i].src1_ready && RS[i].src2_ready)
        {
            if ((RS[i].opcode_sq == OP_ADD || RS[i].opcode_sq == OP_BR || RS[i].opcode_sq == 7) && J > 0)
            {
                FU fuObject;
                fuObject.tag = RS[i].tag;
                fuObject.dest_reg = RS[i].dest_reg;
                fuObject.latency = 1;
                Adder.push_back(fuObject);
                J = J - 1;
            }
            else if (RS[i].opcode_sq == OP_MUL)
            {
                if (K > 0)
                {
                    FU fuObject;
                    fuObject.tag = RS[i].tag;
                    fuObject.dest_reg = RS[i].dest_reg;
                    fuObject.latency = 3;
                    Multiplier.push_back(fuObject);
                    K = K - 1;
                }
                else if ((K == 0) && (Multiplier.size() < 3) && Multiplier[Multiplier.size() - 1].latency < 3)
                {
                    FU fuObject;
                    fuObject.tag = RS[i].tag;
                    fuObject.dest_reg = RS[i].dest_reg;
                    fuObject.latency = 3;
                    Multiplier.push_back(fuObject);
                }
            }
            else if (RS[i].opcode_sq == OP_LOAD)
            {
                if (L > 0)
                {
                    FU fuObject;
                    fuObject.tag = RS[i].tag;
                    fuObject.dest_reg = RS[i].dest_reg;
                    fuObject.latency = 2;
                    Store_Loader.push_back(fuObject);
                    L = L - 1;
                }
                else if ((L == 0) && (Store_Loader.size() < 2) && Store_Loader[Store_Loader.size() - 1].latency < 2)
                {
                    FU fuObject;
                    fuObject.tag = RS[i].tag;
                    fuObject.dest_reg = RS[i].dest_reg;
                    fuObject.latency = 2;
                    Store_Loader.push_back(fuObject);
                }
            }
            else
            {
                FU fuObject;
                fuObject.tag = RS[i].tag;
                fuObject.dest_reg = RS[i].dest_reg;
                fuObject.latency = 1;
                Store_Loader.push_back(fuObject);
                L = L - 1;
            }
        }
    }
}

static void execute(procsim_stats *stats)
{
    for (uint64_t i = 0; i < Adder.size(); i++)
    {
        Adder[i].latency = Adder[i].latency - 1;
        if (Adder[i].latency == 0)
        {
            for (uint64_t jj = 0; jj < RS.size(); jj++)
            {
                if (RS[jj].tag == Adder[i].tag)
                {
                    if (RS[jj].dest_reg != -1)
                    {
                        for (uint64_t kj = 0; kj < RegF.size(); kj++)
                        {
                            if (RegF[kj].preg == RS[jj].dest_reg)
                            {
                                RegF[i].ready = true;
                                break;
                            }
                        }
                    }
                    RS.erase(RS.begin() + jj);
                    break;
                }
            }

            for (uint64_t kk = 0; kk < ROB_T.size(); kk++)
            {

                if (ROB_T[kk].tag == Adder[i].tag)
                {
                    ROB_T[i].ready = true;
                    break;
                }
            }

            Adder.erase(Adder.begin() + i);
            J = J + 1;
        }
    }

    for (uint64_t i = 0; i < Multiplier.size(); i++)
    {
        if (Multiplier[i].latency == 0)
        {
            for (uint64_t jj = 0; jj < RS.size(); jj++)
            {
                if (RS[jj].tag == Multiplier[i].tag)
                {
                    if (RS[jj].dest_reg != -1)
                    {
                        for (uint64_t kj = 0; kj < RegF.size(); kj++)
                        {
                            if (RegF[kj].preg == RS[jj].dest_reg)
                            {
                                RegF[i].ready = true;
                                break;
                            }
                        }
                    }
                    RS.erase(RS.begin() + jj);
                    break;
                }
            }
            for (uint64_t kk = 0; kk < ROB_T.size(); kk++)
            {

                if (ROB_T[kk].tag == Multiplier[i].tag)
                {
                    ROB_T[i].ready = true;
                    break;
                }
            }
            Multiplier.erase(Multiplier.begin() + i);
            if (K < Max_K)
            {
                K++;
            }
        }
    }

    for (uint64_t i = 0; i < Store_Loader.size(); i++)
    {
        if (Store_Loader[i].latency == 0)
        {
            for (uint64_t jj = 0; jj < RS.size(); jj++)
            {
                if (RS[jj].tag == Store_Loader[i].tag)
                {
                    if (RS[jj].dest_reg != -1)
                    {
                        for (uint64_t kj = 0; kj < RegF.size(); kj++)
                        {
                            if (RegF[kj].preg == RS[jj].dest_reg)
                            {
                                RegF[i].ready = true;
                                break;
                            }
                        }
                    }
                    RS.erase(RS.begin() + jj);
                    break;
                }
            }
            for (uint64_t kk = 0; kk < ROB_T.size(); kk++)
            {

                if (ROB_T[kk].tag == Store_Loader[i].tag)
                {
                    ROB_T[i].ready = true;
                    break;
                }
            }
            Store_Loader.erase(Store_Loader.begin() + i);
            if (L < Max_L)
            {
                L++;
            }
        }
    }
}

static void state_update(procsim_stats *stats)
{

    // for (uint64_t i = 0; i < ROB_T.size(); i++)
    // {

    //     cout << ROB_T[i].tag << "i: " << i << endl;
    // }

    for (int i = 0; i < ROB_T.size(); i++)
    {
        if (!ROB_T.empty() && ROB_T[0].ready != false)
        {
            // if (ROB_T[0].ready != false)
            // {
            if (ROB_T[0].prev_preg < 0)
            {
                GLOBAL_STALL = false;
                // ROB_T.erase(ROB_T.begin());
                // stats->instructions_retired++;
            }
            else
            {
                for (uint64_t i = 0; i < RegF.size(); i++)
                {
                    if (ROB_T[0].prev_preg == RegF[i].preg)
                    {
                        RegF[i].free = true;
                        break;
                    }
                }

                for (uint64_t jj = 0; jj < RAT.size(); jj++)
                {
                    if (ROB_T[0].regno == jj)
                    {
                        RAT[jj] = (int32_t)jj;
                        break;
                    }
                }
            }
            stats->instructions_retired++;
            ROB_T.erase(ROB_T.begin());
        }
    }
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

        // cout << read_instruc << "     D: " << DQ.size() << endl;

        // if (!read_instruc && ROB_T.empty() && RS.empty() && DQ.empty())
        // {
        //     ALL_DONE = true;
        // }

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
