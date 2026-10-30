/*
 * logger.h - FR3 concurrent activity logger (pthreads bounded buffer).
 */
#ifndef CC_LOGGER_H
#define CC_LOGGER_H

#define LOGGER_MAX_PRODUCERS 16
#define LOGGER_MAX_CONSUMERS 8
#define LOGGER_MAX_ITEMS     1000
#define LOGGER_MAX_CAPACITY  64

int cmd_logger(int producers, int consumers, int items_per_producer, int capacity);

#endif
