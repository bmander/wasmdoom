// GPL-2.0-or-later. Small CPU policy network shared by training and the engine.
#ifndef P_POLICY_NET_H
#define P_POLICY_NET_H
#define POLICY_INPUTS 12
#define POLICY_HIDDEN 8
#define POLICY_OUTPUTS 5
#define POLICY_WEIGHTS ((POLICY_INPUTS + 1) * POLICY_HIDDEN + (POLICY_HIDDEN + 1) * POLICY_OUTPUTS)
typedef struct { int enabled; float weights[POLICY_WEIGHTS]; } policy_net_t;
void P_PolicyNetLoad(policy_net_t *net, const float *weights);
void P_PolicyNetEvaluate(const policy_net_t *net, const float *input, float *output);
#endif
