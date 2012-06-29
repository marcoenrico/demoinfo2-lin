//====== Copyright (c) 2012, Valve Corporation, All rights reserved. ========//
//
// Redistribution and use in source and binary forms, with or without 
// modification, are permitted provided that the following conditions are met:
//
// Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
// Redistributions in binary form must reproduce the above copyright notice, 
// this list of conditions and the following disclaimer in the documentation 
// and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE 
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF 
// THE POSSIBILITY OF SUCH DAMAGE.
//===========================================================================//

#include <assert.h>
#include <stdio.h>
#include "demofile.h"
#include "snappy.h"

/**
 * Decodes a uint32 from the string of variable length and updates the index into the buffer
 */ 
uint32 ReadVarInt32( const std::string& buf, size_t& index )
{
	uint32 b;
	int count = 0;
	uint32 result = 0;

	do
	{
		if ( count == 5 )
		{
			// If we get here it means that the fifth bit had its
			// high bit set, which implies corrupt data.
			assert( 0 );
			return result;
		}
		else if ( index >= buf.size() )
		{
			assert( 0 );
			return result;
		}

		b = buf[ index++ ];
		result |= ( b & 0x7F ) << ( 7 * count );
		++count;
	} while ( b & 0x80 );

	return result;
}

CDemoFile::CDemoFile() :
	m_fileBufferPos( 0 )
{
}

CDemoFile::~CDemoFile()
{
	Close();//unclear why they are closing this
}

/**
 * Checks if the end of the file buffer has been reached
 *
 * @return true if the buffer has been finished
 */ 
bool CDemoFile::IsDone()
{
	return m_fileBufferPos >= m_fileBuffer.size();
}

/**
 * Determine which of the messages in demo.proto comes next.
 *
 * @param pTick Optional Will provide the updated tick count with the message
 * @param pbCompressed Optional Will be true if the following message is compressed
 * @return The type of the message that should come next in the file
 */ 
EDemoCommands CDemoFile::ReadMessageType( int *pTick, bool *pbCompressed )
{

	uint32 Cmd = ReadVarInt32( m_fileBuffer, m_fileBufferPos );

	if( pbCompressed )//This is a null check
		*pbCompressed = !!( Cmd & DEM_IsCompressed );//Double negation to go from uint32 to bool without truncation issues.

	Cmd = ( Cmd & ~DEM_IsCompressed );//the second three bits (0x70) are being used to say if it's compressed not sure why 3 bits. This may increase the total number of allowable values in overloaded field

	int Tick = ReadVarInt32( m_fileBuffer, m_fileBufferPos );
	if( pTick )//Another null check
		*pTick = Tick;

	if( m_fileBufferPos >= m_fileBuffer.size() )//This would indicate that we'd finished the string already.
		return DEM_Error;//If we'd actually gone > rather than = random memory would have been read.

	return ( EDemoCommands )Cmd;
}

/**
 * @param pMsg The demo message to parse the uncompressed buffer
 * @param bCompressed - Whether or not the message is compressed
 * @param 
 */ 
bool CDemoFile::ReadMessage( IDemoMessage *pMsg, bool bCompressed, int *pSize, int *pUncompressedSize )
{
	int Size = ReadVarInt32( m_fileBuffer, m_fileBufferPos );

	if( pSize )
	{
		*pSize = Size;
	}
	if( pUncompressedSize )//Assume we set this to zero so it doesn't get used by accident
	{
		*pUncompressedSize = 0;
	}

	if( m_fileBufferPos + Size > m_fileBuffer.size() )
	{
		assert( 0 );
		return false;
	}

	if( pMsg )//we don't bother actually reading it if they don't care about the results.
	{
		const char *parseBuffer = &m_fileBuffer[ m_fileBufferPos ];
		m_fileBufferPos += Size;

		if( bCompressed )
		{
			if ( snappy::IsValidCompressedBuffer( parseBuffer, Size ) )
			{
				size_t uDecompressedLen;

				if ( snappy::GetUncompressedLength( parseBuffer, Size, &uDecompressedLen ) )
				{
					if( pUncompressedSize )
					{
						*pUncompressedSize = uDecompressedLen;
					}

					m_parseBufferSnappy.resize( uDecompressedLen );//we checked how big it was now we give ourselves the space
					char *parseBufferUncompressed = &m_parseBufferSnappy[ 0 ];

					if ( snappy::RawUncompress( parseBuffer, Size, parseBufferUncompressed ) )
					{
						if ( pMsg->GetProtoMsg().ParseFromArray( parseBufferUncompressed, uDecompressedLen ) )
						{
							return true;
						}
					}
				}
			}

			assert( 0 );
			fprintf( stderr, "CDemoFile::ReadMessage() snappy::RawUncompress failed.\n" );
			return false;
		}

		return pMsg->GetProtoMsg().ParseFromArray( parseBuffer, Size ); //I would have put this in an else
	}
	else//even if they don't care about the message we still need to move past the message we were supposed to read
	{
		m_fileBufferPos += Size;
		return true;
	}
}

/**
 * Opens a file and reads the entire thing into memory and check that it appears to be the right type
 * @param name the name of the file to open
 */ 
bool CDemoFile::Open( const char *name )
{
	Close();//Initializes the values

	FILE *fp = fopen( name, "rb" );
	if( fp )
	{
		size_t Length;
		protodemoheader_t DotaDemoHeader;

		fseek( fp, 0, SEEK_END );
		Length = ftell( fp );
		fseek( fp, 0, SEEK_SET );//This was just to get the size.

		if( Length < sizeof( DotaDemoHeader ) )
		{
			fprintf( stderr, "CDemoFile::Open: file too small. %s.\n", name );
			return false;
		}

		fread( &DotaDemoHeader, 1, sizeof( DotaDemoHeader ), fp );
		Length -= sizeof( DotaDemoHeader );

		if( strcmp( DotaDemoHeader.demofilestamp, PROTODEMO_HEADER_ID ) )
		{
			fprintf( stderr, "CDemoFile::Open: demofilestamp doesn't match. %s.\n", name );
			return false;
		}

		m_fileBuffer.resize( Length );//apparently we read in the entire file into memory at once
		fread( &m_fileBuffer[ 0 ], 1, Length, fp );
		fclose( fp );
		fp = NULL;
	}

  //This is the wrong way to check the return value. They should be checking that
  //they read in the expected length.
	if ( !m_fileBuffer.size() )
	{
		fprintf( stderr, "CDemoFile::Open: couldn't open file %s.\n", name );
		Close();
		return false;
	}

	m_fileBufferPos = 0;
	m_szFileName = name;
	return true;
}

/**
 * Initializes the per file values
 *
 */ 
void CDemoFile::Close()
{
	m_szFileName.clear();

	m_fileBufferPos = 0;
	m_fileBuffer.clear();

	m_parseBufferSnappy.clear();
}

