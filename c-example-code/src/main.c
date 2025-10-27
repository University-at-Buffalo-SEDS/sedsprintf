#include "telemetry.h"
#include "sedsprintf.h"


int main(void)
{
    //syncronus code
    init_telemetry_router();
    const float data[3] = {37.7749f, -122.4194f, 30.0f};
    log_telemetry_synchronous(SEDS_DT_GPS, data, 3, sizeof(data[0]));
    //asyncronus coder
    //this would be in send routine of the data collector
    log_telemetry_asynchronous(SEDS_DT_GPS, data, 3, sizeof(data[0]));
    //this would be in the main loop of the program or in a freertos task.
    process_all_queues_timeout(20);
    // dispatch_tx_queue_timeout(100);

    //this would be in the isr of the receiver
    //rx_asynchronous(received_bytes, received_length);

    //


    return 0;
}
