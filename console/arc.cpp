#include "arc.h"

ARC::ARC()
{
    set_coordinates();
}

void ARC::set_coordinates(){
    coordinates = xy_t{x: 8, y: 0};
}
