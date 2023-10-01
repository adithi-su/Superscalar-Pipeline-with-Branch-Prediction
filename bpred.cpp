// bpred.cpp
// Implements the branch predictor class.
// Adithi S Upadhya

#include "bpred.h"
#include <vector> //not needed here, switched to traditional array
#include <algorithm>
/**
 * To construct a branch predictor with the given policy.
 * 
 * @param policy the policy this branch predictor should use
 */
BPred::BPred(BPredPolicy policy)
{
    if (policy == 1)
        this->policy = BPRED_ALWAYS_TAKEN;
    else if (policy == 2)
        this->policy = BPRED_GSHARE;
    else if (policy == 0)
        this->policy = BPRED_PERFECT;

    stat_num_branches = 0;
    stat_num_mispred = 0;
    std::fill(PHT, PHT+4096, 2); // initialize each counter to 10 (=2) state
    PHT_index = 0; 
    GHR = 0;
}

/**
 * To get a prediction for the branch with the given address.
 * 
 * @param pc the address (program counter) of the branch to predict
 * @return the prediction for whether the branch is taken or not taken
 */
BranchDirection BPred::predict(uint64_t pc)
{

    if (policy == BPRED_ALWAYS_TAKEN)
        return TAKEN;
    else if (policy == BPRED_GSHARE)
    {
        /*
        - NOTE: to get the lower 12 bits of PC 
        - original_value & ((1 << N) - 1) where N = 12
        - The value ((1 << 12) - 1) = 0xFFF or 4095
        */

        //PHT index is computed before GHR update
        /* Possible PHT values = 00 / 01 / 10 / 11 for taken/non-taken categories
        - PHT index = GHR(12 bit) (XOR) lower 12 bits from PC
        */

        PHT_index = (GHR & 4095) ^ (pc & 4095);
        //if PHT has been updated to TAKEN, the default value will increment
        if (PHT[PHT_index] >= 2)
            return TAKEN;
        else 
            return NOT_TAKEN;
    }
    else
        return TAKEN;

}


/**
 * To update the branch predictor statistics 
 * 
 * @param pc the address (program counter) of the branch
 * @param prediction the prediction made by the branch predictor
 * @param resolution the actual outcome of the branch
 */
void BPred::update(uint64_t pc, BranchDirection prediction,
                   BranchDirection resolution)
{
    stat_num_branches++;
    if (prediction != resolution)
        stat_num_mispred++;
    if (policy == BPRED_GSHARE)
    {
        PHT_index = (GHR & 4095) ^ (pc & 4095); //bitwise xor
        if (resolution == TAKEN)
        {
            GHR = (GHR << 1) + 1;
            PHT[PHT_index] = sat_increment(PHT[PHT_index], 3);
        }
        else if (resolution == NOT_TAKEN)
        {
            GHR = (GHR << 1) + 0;
            PHT[PHT_index] = sat_decrement(PHT[PHT_index]);
        }
    }
}
