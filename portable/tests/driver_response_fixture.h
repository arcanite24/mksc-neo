#ifndef PORTABLE_TEST_DRIVER_RESPONSE_FIXTURE_H
#define PORTABLE_TEST_DRIVER_RESPONSE_FIXTURE_H
#include "../core/driver_steering.h"

/* Authored response profile for ROM-free fixtures; not an original table. */
static inline const PortableDriverResponseRules *portable_test_response_profile(void)
{
    static const PortableDriverResponseRules rules={
        .rows={{-7,64,512,48,64,80,96,1024},{-7,64,512,48,64,80,96,1024},
            {-7,64,512,48,64,80,96,1024},{-7,64,512,48,64,80,96,1024},
            {-7,64,512,48,64,80,96,1024},{-7,64,512,48,64,80,96,1024},
            {-7,64,512,48,64,80,96,1024},{-7,64,512,48,64,80,96,1024}},
        .selector_rows={2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2}
    };
    return &rules;
}

#endif
