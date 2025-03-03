// made by fokkonaut

#include <engine/shared/protocol.h>

#ifndef MASK256
#define MASK256
struct Mask256
{
	int64_t m_aMask[4];

	Mask256()
	{
		m_aMask[0] = -1LL;
		m_aMask[1] = -1LL;
		m_aMask[2] = -1LL;
		m_aMask[3] = -1LL;
	}

	Mask256(const Mask256&) = default;

	Mask256(int ClientID)
	{
		m_aMask[0] = 0;
		m_aMask[1] = 0;
		m_aMask[2] = 0;
		m_aMask[3] = 0;

		if (ClientID == -1)
			return;

		if (ClientID < VANILLA_MAX_CLIENTS)
			m_aMask[0] = 1LL<<ClientID;
		else if (ClientID < VANILLA_MAX_CLIENTS * 2)
			m_aMask[1] = 1LL<<(ClientID-VANILLA_MAX_CLIENTS);
		else if (ClientID < VANILLA_MAX_CLIENTS * 3)
			m_aMask[2] = 1LL << (ClientID - VANILLA_MAX_CLIENTS * 2);
		else
			m_aMask[3] = 1LL << (ClientID - VANILLA_MAX_CLIENTS * 3);
	}

	Mask256(int64_t Mask0, int64_t Mask1, int64_t Mask2, int64_t Mask3)
	{
		m_aMask[0] = Mask0;
		m_aMask[1] = Mask1;
		m_aMask[2] = Mask2;
		m_aMask[3] = Mask3;
	}

	int64_t operator[](int ID)
	{
		return m_aMask[ID];
	}

	Mask256 operator~() const
	{
		return Mask256(~m_aMask[0], ~m_aMask[1], ~m_aMask[2], ~m_aMask[3]);
	}

	Mask256 operator^(Mask256 Mask)
	{
		return Mask256(m_aMask[0]^Mask[0], m_aMask[1]^Mask[1], m_aMask[2]^Mask[2], m_aMask[3]^Mask[3]);
	}

	Mask256 operator=(Mask256 Mask)
	{
		m_aMask[0] = Mask[0];
		m_aMask[1] = Mask[1];
		m_aMask[2] = Mask[2];
		m_aMask[3] = Mask[3];
		return *this;
	}

	void operator|=(Mask256 Mask)
	{
		m_aMask[0] |= Mask[0];
		m_aMask[1] |= Mask[1];
		m_aMask[2] |= Mask[2];
		m_aMask[3] |= Mask[3];
	}

	Mask256 operator&(Mask256 Mask)
	{
		return Mask256(m_aMask[0]&Mask[0], m_aMask[1]&Mask[1], m_aMask[2]&Mask[2], m_aMask[3]&Mask[3]);
	}

	void operator&=(Mask256 Mask)
	{
		m_aMask[0] &= Mask[0];
		m_aMask[1] &= Mask[1];
		m_aMask[2] &= Mask[2];
		m_aMask[3] &= Mask[3];
	}

	bool operator==(Mask256 Mask)
	{
		return m_aMask[0] == Mask[0] && m_aMask[1] == Mask[1] && m_aMask[2] == Mask[2] && m_aMask[3] == Mask[3];
	}

	bool operator!=(Mask256 Mask)
	{
		return m_aMask[0] != Mask[0] || m_aMask[1] != Mask[1] || m_aMask[2] != Mask[2] || m_aMask[3] != Mask[3];
	}
};
#endif
