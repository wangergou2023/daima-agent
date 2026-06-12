#pragma once

#include "agent/agent_coordinator.h"
#include "agent/agent_roles.h"
#include "agent/category_router.h"
#include "agent/intent_gate.h"
#include "bus/message_bus.h"
#include "daima_err.h"

daima_err_t agent_status_push_agent_state(const daima_msg_t *msg,
                                          daima_intent_t intent,
                                          agent_role_t role);
daima_err_t agent_status_push_agent_state_clear(const daima_msg_t *msg,
                                                daima_intent_t intent);
daima_err_t agent_status_push_coordinator_status(const daima_msg_t *msg,
                                                 const coordinator_t *coord);
