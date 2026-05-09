#pragma once

#include "Emu/Io/pad_types.h"

namespace tas_input
{
	void handle_pad_data(u32 port_no, CellPadData& data);
}
