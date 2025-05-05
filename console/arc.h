#include "tile.h"

class ARC : public Tile
{

    void set_coordinates() override;

public:
    ARC();

    // Probably need an arc messaging interface implemented
};
