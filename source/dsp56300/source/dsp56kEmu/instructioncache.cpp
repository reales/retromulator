#include "instructioncache.h"
#include "memory.h"

namespace dsp56k
{
	const TWord g_initPattern = 0xffabcabc;

	// _____________________________________________________________________________
	// reset
	//
	void InstructionCache::reset()
	{
		for( TSectorIdx s=0; s<eNumSectors; ++s )
		{
			m_lruStack[s] = eNumSectors-s-1;

			flushSector( s );
		}
	}

	// _____________________________________________________________________________
	// flushSector
	//
	void InstructionCache::flushSector( TSectorIdx s )
	{
		m_sectorLocked	[s] = false;
		m_tagRegister	[s] = eMaskTAG;

		for( TWord i=0; i<eSectorSize; ++i )
		{
			m_validBits	[s][i] = false;
			m_memory	[s][i] = g_initPattern;
		}
	}

	// _____________________________________________________________________________
	// fetch
	//
	dsp56k::TWord InstructionCache::fetch( Memory& _mem, TWord _address, bool _burst )
	{
		return _mem.get( MemArea_P, _address );
	}

	// _____________________________________________________________________________
	// findLRUSector
	//
	InstructionCache::TSectorIdx InstructionCache::findLRUSector()
	{
		for( int i=eNumSectors-1; i>=0; --i )
		{
			TSectorIdx s = m_lruStack[i];
			if( !m_sectorLocked[s] )
				return s;
		}
		return eSectorInvalid;
	}

	// _____________________________________________________________________________
	// setSectorMRU
	//
	void InstructionCache::setSectorMRU( TSectorIdx _s )
	{
		if( m_lruStack[0] == _s )
			return;	// no change required

		TSectorIdx temp = m_lruStack[0];

		m_lruStack[0] = _s;

		// shift sectors upward until our own sector is found, that is placed into 0
		for( unsigned int i=1; i<m_lruStack.size(); ++i )
		{
			if( m_lruStack[i] == _s )
			{
				m_lruStack[i] = temp;
				return;
			}
			std::swap( temp, m_lruStack[i] );
		}

	}
	// _____________________________________________________________________________
	// lock
	//
	bool InstructionCache::plock( TWord _address )
	{
		TSectorIdx s = createSectorForAddress(_address);

		if( s == eSectorInvalid )
		{
			return false;
		}

		m_sectorLocked[s] = true;

		return true;
	}

	// _____________________________________________________________________________
	// initializeSector
	//
	bool InstructionCache::initializeSector( TSectorIdx s, TWord _address )
	{
		const TWord tag = _address & eMaskTAG;

		m_tagRegister[s] = tag;

		for( size_t i=0; i<m_memory[s].size(); ++i )
		{
			m_validBits[s][i] = false;
			m_memory[s][i] = g_initPattern;
		}

		return true;
	}

	// _____________________________________________________________________________
	// createSectorForAddress
	//
	InstructionCache::TSectorIdx InstructionCache::createSectorForAddress( TWord _address )
	{
		TSectorIdx s = findSectorByAddress( _address );

		if( s != eSectorInvalid )
		{
			// sector already cached
			setSectorMRU(s);
			return s;
		}

		s = findLRUSector();

		if( s == eSectorInvalid )
		{
			// all sectors already locked and in use by other memory areas
			return eSectorInvalid;
		}

		setSectorMRU(s);
		initializeSector( s, _address );
		return s;	
	}
	// _____________________________________________________________________________
	// pfree
	//
	void InstructionCache::pfree()
	{
		for( int i=0; i<eNumSectors; ++i )
			m_sectorLocked[i] = false;
	}
	// _____________________________________________________________________________
	// pflushun
	//
	void InstructionCache::pflushun()
	{
		// TODO
	}
}
