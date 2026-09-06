// GPL-2.0-or-later. Small CPU policy network shared by training and the engine.
#ifndef P_POLICY_NET_H
#define P_POLICY_NET_H
#define POLICY_INPUTS 12
#define POLICY_HIDDEN 8
#define POLICY_OUTPUTS 5
#define POLICY_WEIGHTS ((POLICY_INPUTS + 1) * POLICY_HIDDEN + (POLICY_HIDDEN + 1) * POLICY_OUTPUTS)
// Version 4: 24 observations + 8 memory values -> 32 hidden -> 10 actions + 8 memory.
#define TACTIC_INPUTS 24
#define TACTIC_MEMORY 8
#define TACTIC_HIDDEN 32
#define TACTIC_OUTPUTS 10
#define TACTIC_WEIGHTS ((TACTIC_INPUTS + TACTIC_MEMORY + 1) * TACTIC_HIDDEN + (TACTIC_HIDDEN + 1) * (TACTIC_OUTPUTS + TACTIC_MEMORY))
typedef struct { int enabled, recurrent; float weights[TACTIC_WEIGHTS]; } policy_net_t;
void P_PolicyNetLoad(policy_net_t *net, const float *weights);
void P_PolicyNetEvaluate(const policy_net_t *net, const float *input, float *output);
void P_PolicyNetLoadRecurrent(policy_net_t *net, const float *weights);
void P_PolicyNetRecurrent(const policy_net_t *net, const float *input, float *memory, float *output);
#endif
