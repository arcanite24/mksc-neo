#include "../platform/controller_input.h"
#include "../core/driver_input.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do{if(!(x)){fprintf(stderr,"controller input failed line %d: %s\n",__LINE__,#x);exit(1);}}while(0)

int main(void)
{
    CHECK(portable_controller_driving_buttons(NULL)==0);
    PortableControllerSample s={0};
    s.right_trigger=7999;s.left_trigger=7999;s.left_x=7999;
    CHECK(portable_controller_driving_buttons(&s)==0);
    s=(PortableControllerSample){.left_x=12000,.right_trigger=8000};
    CHECK(portable_controller_driving_buttons_deadzone(&s,16000u)==PORTABLE_INPUT_ACCELERATE);
    CHECK(portable_controller_driving_buttons_deadzone(&s,12000u)==(PORTABLE_INPUT_ACCELERATE|PORTABLE_INPUT_RIGHT));
    s.dpad_left=1;
    CHECK(portable_controller_driving_buttons_deadzone(&s,30000u)==(PORTABLE_INPUT_ACCELERATE|PORTABLE_INPUT_LEFT));
    CHECK(portable_controller_driving_buttons_deadzone(&s,0u)==0u);
    s=(PortableControllerSample){.right_trigger=8000,.left_trigger=8000,.left_x=-8000};
    CHECK(portable_controller_driving_buttons(&s)==
        (PORTABLE_INPUT_ACCELERATE|PORTABLE_INPUT_BRAKE|PORTABLE_INPUT_LEFT));
    s=(PortableControllerSample){.south=1,.east=1,.left_x=8000};
    CHECK(portable_controller_driving_buttons(&s)==
        (PORTABLE_INPUT_ACCELERATE|PORTABLE_INPUT_BRAKE|PORTABLE_INPUT_RIGHT));
    s=(PortableControllerSample){.dpad_left=1,.left_x=16000};
    CHECK(portable_controller_driving_buttons(&s)==PORTABLE_INPUT_LEFT);
    s=(PortableControllerSample){.dpad_left=1,.dpad_right=1,.left_x=-16000};
    CHECK(portable_controller_driving_buttons(&s)==0);
    puts("controller input: PASS");
    return 0;
}
