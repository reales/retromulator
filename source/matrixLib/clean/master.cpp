#include "master.h"

namespace matrixClean
{
	void Master::reinit()
	{
		data.fill(0);
		data[1] = 40;
		data[6] = 1;
		data[7] = 63;
		data[10] = 63;
		data[12] = 1;
		data[13] = 1;
		data[14] = 1;
		data[15] = 1;
		data[17] = 4;
		data[18] = 64;
		data[19] = 1;
		data[20] = 2;
		data[23] = 27;
		data[27] = 1;
		data[28] = 5;
		data[29] = 5;
		data[31] = 1;
		for(size_t i = 36; i <= 161; ++i)
			data[i] = 0xff;
		data[162] = 81;
		data[164] = 2;
		data[165] = 0x80;
		data[166] = 1;
	}

	void Master::receive(const std::array<uint8_t, Size>& _data)
	{
		data = _data;
		data[165] &= 0x80;
	}
}
