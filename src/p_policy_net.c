// GPL-2.0-or-later. 12 -> 8 -> 5 tanh MLP, 149 trainable weights/biases.
#include <math.h>
#include <string.h>
#include "p_policy_net.h"

void P_PolicyNetLoad(policy_net_t *net, const float *weights)
{
    memset(net, 0, sizeof(*net));
    if (!weights) return;
    for (int i = 0; i < POLICY_WEIGHTS; i++) {
        if (!isfinite(weights[i]) || weights[i] < -4 || weights[i] > 4) return;
    }
    memcpy(net->weights, weights, POLICY_WEIGHTS * sizeof(*weights));
    net->enabled = 1;
}

void P_PolicyNetEvaluate(const policy_net_t *net, const float *input, float *output)
{
    float hidden[POLICY_HIDDEN];
    memset(output, 0, POLICY_OUTPUTS * sizeof(*output));
    if (!net->enabled) return;
    int at = 0;
    for (int h = 0; h < POLICY_HIDDEN; h++) {
        float sum = net->weights[at++];
        for (int i = 0; i < POLICY_INPUTS; i++) {
            float feature = input[i] < -1 ? -1 : input[i] > 1 ? 1 : input[i];
            sum += net->weights[at++] * feature;
        }
        hidden[h] = tanhf(sum);
    }
    for (int o = 0; o < POLICY_OUTPUTS; o++) {
        float sum = net->weights[at++];
        for (int h = 0; h < POLICY_HIDDEN; h++) sum += net->weights[at++] * hidden[h];
        output[o] = tanhf(sum);
    }
}

void P_PolicyNetLoadRecurrent(policy_net_t *net, const float *weights)
{
    memset(net, 0, sizeof(*net));
    if (!weights) return;
    for (int i = 0; i < TACTIC_WEIGHTS; i++)
        if (!isfinite(weights[i]) || fabsf(weights[i]) > 4) return;
    memcpy(net->weights, weights, TACTIC_WEIGHTS * sizeof(*weights));
    net->enabled = net->recurrent = 1;
}

void P_PolicyNetRecurrent(const policy_net_t *net, const float *input, float *memory, float *output)
{
    float hidden[TACTIC_HIDDEN], next[TACTIC_MEMORY];
    memset(output, 0, TACTIC_OUTPUTS * sizeof(*output));
    if (!net->enabled || !net->recurrent) return;
    int at = 0;
    for (int h = 0; h < TACTIC_HIDDEN; h++) {
        float sum = net->weights[at++];
        for (int i = 0; i < TACTIC_INPUTS + TACTIC_MEMORY; i++) {
            float v = i < TACTIC_INPUTS ? input[i] : memory[i - TACTIC_INPUTS];
            v = v < -1 ? -1 : v > 1 ? 1 : v;
            sum += net->weights[at++] * v;
        }
        hidden[h] = tanhf(sum);
    }
    for (int o = 0; o < TACTIC_OUTPUTS + TACTIC_MEMORY; o++) {
        float sum = net->weights[at++];
        for (int h = 0; h < TACTIC_HIDDEN; h++) sum += net->weights[at++] * hidden[h];
        if (o < TACTIC_OUTPUTS) output[o] = tanhf(sum);
        else next[o - TACTIC_OUTPUTS] = tanhf(sum);
    }
    memcpy(memory, next, sizeof(next));
}
