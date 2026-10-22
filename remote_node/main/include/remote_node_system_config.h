/**
 * @file remote_node_system_config.h
 * @author Anthony Yalong
 * @brief Remote node pin assignments and task timing.
 */

#ifndef REMOTE_NODE_SYSTEM_CONFIG_H
#define REMOTE_NODE_SYSTEM_CONFIG_H

#include <driver/gpio.h>

#define HCSR501_GPIO_PIN GPIO_NUM_13

#define MOTION_SAMPLE_TASK_STACK_BYTES 4096
#define MOTION_SAMPLE_TASK_PRIORITY 5
#define MOTION_SAMPLE_TASK_PERIOD_MS 5000

#endif // REMOTE_NODE_SYSTEM_CONFIG_H
