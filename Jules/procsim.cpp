
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
// bool freePreg = false;

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
uint64_t R;

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
    uint64_t fetch_cycle; // cycle in which fetched
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
    R = conf->R;

    Max_K = conf->K;
    Max_L = conf->L;

    ROB_size = conf->R;
    RS_max_value = 2 * (J + K + L);

    // Note - The schedule queue can hold a maximum of 2 * (J + K + L) instructions

    //INITIALIZING THE REGISTER FILE AND THE RAT
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

/*******************************************************************************
****************************** HELPER FUNCTION *********************************
********************************************************************************/

//RETURNS THE INDEX OF THE FIRST FREE PREG
uint64_t freePRegIndex()
{
    for (uint64_t i = 32; i < P; i++)
    {
        if (RegF[i].free)
        {
            return i;
        }
    }
}

//CHECKS IF THERE ARE ANY FREE PREGS
bool freePreg()
{
    for (uint64_t i = 32; i < P; i++)
    {
        if (RegF[i].free)
        {
            return true;
        }
    }
    return false;
}

//ASSIGNS VALUES TO THE SCHEDULING QUEUE
void freeRob(int32_t src_dq, int32_t &src_sq, bool &ready)
{
    if (!ROB_T.empty()) //Checks if the ROB is not empty
    {
        for (uint64_t i = (ROB_T.size() - 1); i >= 0; i--)
        {
            if (ROB_T[i].regno == (uint64_t)src_dq) //checks if areg src register if found in the ROB
            {
                if (ROB_T[i].ready)
                {
                    ready = true; // set the ready bit of the source register to ready
                    src_sq = RAT[src_dq];
                }
                else
                {
                    src_sq = ROB_T[i].prev_preg; //src register takes the previous preg
                    ready = false;
                }
                break;
            }
            if (i == 0)
            {
                break;
            }
        }
    }
    src_sq = RAT[src_dq];
    ready = true;
}

// bool robSorting(const ROB &a, ROB &b)
// {
//     return a.tag < b.tag;
// }

static void fetch(procsim_stats *stats)
{
    // Sample for how instructions are read from the trace
    instruction *new_inst = new instruction;
    read_instruc = read_instruction(new_inst);

    if (!read_instruc)
    {
        ALL_DONE = true;
    }

    // IF THERE'S AN EXCEPTION AND THE DQ IS NOT STALLED
    if (RAISE_EXCEPTION && !GLOBAL_STALL)
    {
        DisQ dqObject;
        dqObject.opcode_sq = 7;
        dqObject.tag = tag;
        DQ.insert(DQ.begin(), dqObject); //INSERT THE INTERRUPT AT THE HEAD OF THE DQ
        tag++;
        GLOBAL_STALL = true;
    }

    //ADDS INSTRUCTION TO THE DISPATCH QUEUE
    for (uint64_t i = 0; i < F; i++)
    {
        tag++;
        if (new_inst->opcode == 5) //STORE INSTRUCTIONS
        {
            DisQ dqObject;
            dqObject.address = new_inst->inst_addr;
            dqObject.opcode_sq = new_inst->opcode;
            dqObject.dest_reg = new_inst->dest_reg;
            dqObject.src1_reg = new_inst->src_reg[0];
            dqObject.src2_reg = new_inst->src_reg[1];
            dqObject.tag = tag;
            dqObject.ld_st_addr = new_inst->ld_st_addr;
            dqObject.fetch_cycle = GLOBAL_CLOCK;
            DQ.push_back(dqObject);

            stats->store_instructions++; // stats for store instructions
        }
        else if (new_inst->opcode == 4) //LOAD INSTRUCTIONS
        {
            DisQ dqObject;
            dqObject.address = new_inst->inst_addr;
            dqObject.opcode_sq = new_inst->opcode;
            dqObject.dest_reg = new_inst->dest_reg;
            dqObject.src1_reg = new_inst->src_reg[0];
            dqObject.src2_reg = new_inst->src_reg[1];
            dqObject.tag = tag;
            dqObject.ld_st_addr = new_inst->ld_st_addr;
            dqObject.fetch_cycle = GLOBAL_CLOCK;
            DQ.push_back(dqObject);

            stats->load_instructions++; // stats for store instructions
        }
        else if (new_inst->opcode == 6) //BRANCH INSTRUCTIONS
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
        }
    }
}

static void dispatch(procsim_stats *stats)
{

    while (!DQ.empty() && RS.size() <= RS_max_value && ROB_T.size() < R)
    {
        if (GLOBAL_STALL) // IF THERE'S AN INTERRUPT, GLOBAL STALL IS TRUE, DISPATCH IS STALLED
        {
            break;
        }
        if ((DQ[0].opcode_sq != OP_NOP) && DQ[0].opcode_sq != 7) //CASE IF INSTRUCTION IS NOT NOP OR INTERRUPT
        {
            SQ sqObject;

            if (DQ[0].dest_reg < (0)) //IF DESTINATION REGISTER IS -1
            {
                sqObject.address = DQ[0].address;
                sqObject.opcode_sq = DQ[0].opcode_sq;
                sqObject.dest_reg = -1;
                sqObject.tag = DQ[0].tag;
                if (DQ[0].src1_reg < 0) //IF SOURCE REGISTER -1
                {
                    sqObject.src1_reg = -1;
                    sqObject.src1_ready = true;
                }
                else
                {
                    freeRob(DQ[0].src1_reg, sqObject.src1_reg, sqObject.src1_ready);
                }
                if (DQ[0].src2_reg < 0) //IF SOURCE REGISTER -1
                {
                    sqObject.src2_reg = -1;
                    sqObject.src2_ready = true;
                }
                else
                {
                    freeRob(DQ[0].src2_reg, sqObject.src2_reg, sqObject.src2_ready);
                }
            }
            else
            {
                if (freePreg()) //IF THERE'S A FREE PREG
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

            //ADD ELEMENT TO THE ROB
            ROB robObject;
            robObject.prev_preg = RAT[DQ[0].dest_reg];
            robObject.regno = DQ[0].dest_reg;
            robObject.ready = false;
            robObject.tag = DQ[0].tag;
            ROB_T.push_back(robObject);
        }
        else if (DQ[0].opcode_sq == 7) // IF INSTRUCTION IS INTERUPT
        {
            SQ sqObject;
            sqObject.dest_reg = -1;
            sqObject.src1_ready = true;
            sqObject.tag = DQ[0].tag;
            sqObject.src2_ready = true;
            RS.push_back(sqObject);

            ROB robObject;
            robObject.prev_preg = -1;
            robObject.regno = -1;
            robObject.ready = false;
            robObject.tag = DQ[0].tag;
            ROB_T.push_back(robObject);

            stats->num_exceptions++; //stats for interrupt instructions
        }
        else
        {
            continue; //IF NO OP
        }

        DQ.erase(DQ.begin());
    }
}

static void schedule(procsim_stats *stats)
{

    for (uint64_t i = 0; i < RS.size(); i++)
    {

        // ADD INSTRUCTIONS TO THE FU IF THEY ARE READY
        if (RS[i].src1_ready && RS[i].src2_ready)
        {
            if ((RS[i].opcode_sq == OP_ADD || RS[i].opcode_sq == OP_BR || RS[i].opcode_sq == 7) && J > 0) //CASE FOR ADD ALU
            {
                FU fuObject;
                fuObject.tag = RS[i].tag;
                fuObject.dest_reg = RS[i].dest_reg;
                fuObject.latency = 1;
                Adder.push_back(fuObject);
                J = J - 1;
            }
            else if (RS[i].opcode_sq == OP_MUL) //CASE FOR MUL ALU
            {
                if (K > 0) //CHECK IF THE NUMBER OF ALU >0
                {
                    FU fuObject;
                    fuObject.tag = RS[i].tag;
                    fuObject.dest_reg = RS[i].dest_reg;
                    fuObject.latency = 3;
                    Multiplier.push_back(fuObject);
                    K = K - 1;
                }
                else if (K == 0) //CASE FOR PIPELINING MUL INSTRUCTIONS
                {
                    // IF THE MUL ALU HAS LESS THAN 4 INST AND THE LAST INST LATENCY IS INFERIOR TO 3, THEN PIPELINE INST
                    if ((Multiplier.size() < 3) && (Multiplier[Multiplier.size() - 1].latency < 3))
                    {
                        FU fuObject;
                        fuObject.tag = RS[i].tag;
                        fuObject.dest_reg = RS[i].dest_reg;
                        fuObject.latency = 3;
                        Multiplier.push_back(fuObject);
                    }
                }
            }
            else if (RS[i].opcode_sq == OP_LOAD) //CASE FOR load ALU
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
                else if (L == 0) //CASE FOR PIPELINING MUL INSTRUCTIONS
                {
                    // IF THE MUL ALU HAS LESS THAN 4 INST AND THE LAST INST LATENCY IS INFERIOR TO 3, THEN PIPELINE INST
                    if ((Store_Loader.size() < 4) && (Store_Loader[Store_Loader.size() - 1].latency < 2))
                    {
                        FU fuObject;
                        fuObject.tag = RS[i].tag;
                        fuObject.dest_reg = RS[i].dest_reg;
                        fuObject.latency = 2;
                        Store_Loader.push_back(fuObject);
                    }
                }
            }
            else
            { //CASE FOR STORE INST
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
    //******************************* ADD ALU ****************************************
    for (uint64_t i = 0; i < Adder.size(); i++) //LOOP THROUGH THE ADD ALU
    {
        Adder[i].latency = Adder[i].latency - 1; //DECREMENTS THE LATENCY
        if (Adder[i].latency == 0)
        {
            //IF LATENCY =0, FIND THE INSTRUCTION IN THE RESERVATION STATION AND SET THE PREG TO READY
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
                                RegF[kj].ready = true;
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
                    ROB_T[kk].ready = true;
                    break;
                }
            }

            Adder.erase(Adder.begin() + i);
            J = J + 1;
        }
    }

    //******************************* MUL ALU ****************************************

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
                                RegF[kj].ready = true;
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
                    ROB_T[kk].ready = true;
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

    //******************************* LOAD ALU ****************************************

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
                                RegF[kj].ready = true;
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
                    ROB_T[kk].ready = true;
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

    //  LOOP THROUGH THE ROB AND REMOVE THE FIRST ELEMENT IF IT IS READY, RETIRING THE INSTRUCTION
    while (true)
    {
        //IF THE ROB IS NOT EMPTY AND THE FIRST ELEMENT IS READY, RETIRE THE INST
        if (!ROB_T.empty() && ROB_T[0].ready != false)
        {
            if (ROB_T[0].prev_preg < 0)
            {
                GLOBAL_STALL = false;
                RAISE_EXCEPTION = false;
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
            }
            stats->instructions_retired++;
            ROB_T.erase(ROB_T.begin());
        }
        else
        {
            break;
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
