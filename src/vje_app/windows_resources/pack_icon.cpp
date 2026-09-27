//---------------------------------------------------------------------------------------------------------------------
// Project: VJE (Versatile JSON Editor) 2.0
// Version: 2.0.0
// Date:    2024
// Author:  Rohin Gosling
//
// Description:
//
//   pack_icon -- a build-time tool: `pack_icon <output.ico> <master.png>...` writes one Windows icon file holding every
//   application-icon master, each as it was drawn (FILE-15, architecture.md section 9.2).
//
//   WHY AN .ico AT ALL. Explorer draws an application's icon from the executable's own icon resource -- the registered
//   entries name `"vje.exe",0`, the first icon in the file -- and QApplication::setWindowIcon sets only the running
//   window's, which Explorer never sees. The resource compiler takes an .ico, so the masters have to become one.
//
//   THE MASTERS ARE COPIED, NOT RESAMPLED. Since Windows Vista an icon entry may be a PNG stream, stored whole, so each
//   master goes into the file byte for byte and the executable carries exactly the pixels the author drew. Sizes with
//   no master are left to Windows, which scales the nearest one -- the same rule QIcon follows for the window icon, and
//   the reason drawing a new size is a drop-in: CMake globs the masters, so a new file reaches both.
//
//   BUILT AND RUN AT BUILD TIME, so the .ico is never committed beside the PNGs and cannot drift from them. It depends
//   on nothing -- no Qt, no image library -- because it only reads each PNG's header and copies its bytes.
//
// TODO:
//
//   1. None.
//
//---------------------------------------------------------------------------------------------------------------------

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
	// The eight bytes every PNG begins with, and where its IHDR chunk keeps the width and height (big-endian).

	constexpr unsigned char PNG_SIGNATURE [] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };

	constexpr std::size_t PNG_WIDTH_OFFSET  = 16;
	constexpr std::size_t PNG_HEIGHT_OFFSET = 20;
	constexpr std::size_t PNG_HEADER_BYTES  = 24;

	// An icon entry stores its size in one byte, with 0 meaning 256 -- so 256 is the largest an entry can be.

	constexpr std::uint32_t MAXIMUM_ICON_SIZE = 256;

	constexpr std::uint32_t ICON_HEADER_BYTES = 6;
	constexpr std::uint32_t ICON_ENTRY_BYTES  = 16;
	constexpr std::uint16_t ICON_TYPE         = 1;         // 1 = icon, 2 = cursor.
	constexpr std::uint16_t BITS_PER_PIXEL    = 32;

	struct Master
	{
		std::string                path;
		std::uint32_t              size = 0;
		std::vector<unsigned char> bytes;
	};

	std::uint32_t read_big_endian ( const std::vector<unsigned char>& bytes, std::size_t offset )
	{
		return ( std::uint32_t ( bytes [ offset ] ) << 24 ) | ( std::uint32_t ( bytes [ offset + 1 ] ) << 16 )
		     | ( std::uint32_t ( bytes [ offset + 2 ] ) << 8 )  |   std::uint32_t ( bytes [ offset + 3 ] );
	}

	void write_little_endian ( std::vector<unsigned char>& output, std::uint32_t value, int bytes )
	{
		for ( int i = 0; i < bytes; ++i )
		{
			output.push_back ( static_cast<unsigned char> ( ( value >> ( 8 * i ) ) & 0xFF ) );
		}
	}

	// Read one master, or say why it cannot be one. A refusal fails the build, which is the point: a master that is
	// not a square PNG of a size an icon can hold would otherwise reach the executable as something the author did
	// not draw.

	bool read_master ( const std::string& path, Master& master, std::string& problem )
	{
		std::ifstream file ( path, std::ios::binary );

		if ( !file )
		{
			problem = "cannot be opened";

			return false;
		}

		master.path  = path;
		master.bytes = std::vector<unsigned char> ( std::istreambuf_iterator<char> ( file ), std::istreambuf_iterator<char> () );

		if ( ( master.bytes.size () < PNG_HEADER_BYTES ) || !std::equal ( std::begin ( PNG_SIGNATURE ), std::end ( PNG_SIGNATURE ), master.bytes.begin () ) )
		{
			problem = "is not a PNG file";

			return false;
		}

		const std::uint32_t width  = read_big_endian ( master.bytes, PNG_WIDTH_OFFSET );
		const std::uint32_t height = read_big_endian ( master.bytes, PNG_HEIGHT_OFFSET );

		if ( ( width != height ) || ( width == 0 ) || ( width > MAXIMUM_ICON_SIZE ) )
		{
			problem = "is " + std::to_string ( width ) + " x " + std::to_string ( height ) + "; an icon master is square and at most 256 px";

			return false;
		}

		master.size = width;

		return true;
	}
}

int main ( int argc, char* argv [] )
{
	if ( argc < 3 )
	{
		std::fprintf ( stderr, "usage: pack_icon <output.ico> <master.png>...\n" );

		return 2;
	}

	std::vector<Master> masters;

	for ( int i = 2; i < argc; ++i )
	{
		Master      master;
		std::string problem;

		if ( !read_master ( argv [ i ], master, problem ) )
		{
			std::fprintf ( stderr, "pack_icon: %s %s.\n", argv [ i ], problem.c_str () );

			return 1;
		}

		masters.push_back ( std::move ( master ) );
	}

	// Smallest first, so the file is the same whatever order the glob listed the masters in. Two masters of one size
	// would leave Windows to pick between them, which is a choice the author has to make instead.

	std::sort ( masters.begin (), masters.end (), [] ( const Master& left, const Master& right ) { return left.size < right.size; } );

	for ( std::size_t i = 1; i < masters.size (); ++i )
	{
		if ( masters [ i ].size == masters [ i - 1 ].size )
		{
			std::fprintf ( stderr, "pack_icon: %s and %s are both %u px.\n", masters [ i - 1 ].path.c_str (), masters [ i ].path.c_str (), masters [ i ].size );

			return 1;
		}
	}

	// ICONDIR, one ICONDIRENTRY per master, then each master's bytes in the same order.

	std::vector<unsigned char> output;

	write_little_endian ( output, 0, 2 );
	write_little_endian ( output, ICON_TYPE, 2 );
	write_little_endian ( output, static_cast<std::uint32_t> ( masters.size () ), 2 );

	std::uint32_t offset = ICON_HEADER_BYTES + ICON_ENTRY_BYTES * static_cast<std::uint32_t> ( masters.size () );

	for ( const Master& master : masters )
	{
		const std::uint32_t storedSize = ( master.size == MAXIMUM_ICON_SIZE ) ? 0 : master.size;

		write_little_endian ( output, storedSize, 1 );                                            // Width.
		write_little_endian ( output, storedSize, 1 );                                            // Height.
		write_little_endian ( output, 0, 1 );                                                     // Palette colours: none.
		write_little_endian ( output, 0, 1 );                                                     // Reserved.
		write_little_endian ( output, 1, 2 );                                                     // Colour planes.
		write_little_endian ( output, BITS_PER_PIXEL, 2 );
		write_little_endian ( output, static_cast<std::uint32_t> ( master.bytes.size () ), 4 );
		write_little_endian ( output, offset, 4 );

		offset += static_cast<std::uint32_t> ( master.bytes.size () );
	}

	for ( const Master& master : masters )
	{
		output.insert ( output.end (), master.bytes.begin (), master.bytes.end () );
	}

	std::ofstream file ( argv [ 1 ], std::ios::binary | std::ios::trunc );

	file.write ( reinterpret_cast<const char*> ( output.data () ), static_cast<std::streamsize> ( output.size () ) );

	if ( !file )
	{
		std::fprintf ( stderr, "pack_icon: cannot write %s.\n", argv [ 1 ] );

		return 1;
	}

	return 0;
}
