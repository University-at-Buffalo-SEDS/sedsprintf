#ifndef SEDSPRINTF_LIBRARY_H
#define SEDSPRINTF_LIBRARY_H
#include "struct_setup.h"
typedef struct
{
    transmit_helper_t * transmit_helpers;
    size_t num_helpers;
} SEDSPRINTF_CONFIG;

typedef enum
{
    OK,
    ERROR,
} SEDSPRINTF_STATUS;


class sedsprintf
{
public:
    sedsprintf();

    ~sedsprintf();

    void init();

    SEDSPRINTF_STATUS log(void * data);

private:
    SEDSPRINTF_STATUS transmit(telemetry_packet_t * packet);

    SEDSPRINTF_STATUS receive(telemetry_packet_t * packet);

    SEDSPRINTF_CONFIG cfg;
};

#endif // SEDSPRINTF_LIBRARY_H
