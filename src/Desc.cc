// $Id: Desc.cc 6245 2008-10-07 00:56:59Z vern $
//
// See the file "COPYING" in the main distribution directory for copyright.

#include "config.h"

#include <stdlib.h>
#include <errno.h>

#include "Desc.h"
#include "File.h"

#define DEFAULT_SIZE 128
#define SLOP 10

ODesc::ODesc(desc_type t, BroFile* arg_f)
	{
	type = t;
	style = STANDARD_STYLE;
	f = arg_f;

	if ( f == 0 )
		{
		size = DEFAULT_SIZE;
		base = safe_malloc(size);
		((char*) base)[0] = '\0';

		offset = 0;

		if ( ! base )
			OutOfMemory();
		}
	else
		{
		offset = size = 0;
		base = 0;
		}

	indent_level = 0;
	is_short = 0;
	want_quotes = 0;
	do_flush = 1;
	include_stats = 0;
	indent_with_spaces = 0;
	escape = 0;
	escape_len = 0;
	}

ODesc::~ODesc()
	{
	if ( f )
		{
		if ( do_flush )
			f->Flush();
		}
	else if ( base )
		free(base);
	}

void ODesc::SetEscape(const char* arg_escape, int len)
	{
	escape = arg_escape;
	escape_len = len;
	}

void ODesc::PushIndent()
	{
	++indent_level;
	NL();
	}

void ODesc::PopIndent()
	{
	if ( --indent_level < 0 )
		internal_error("ODesc::PopIndent underflow");
	NL();
	}

void ODesc::PopIndentNoNL()
	{
	if ( --indent_level < 0 )
		internal_error("ODesc::PopIndent underflow");
	}

void ODesc::Add(const char* s, int do_indent)
	{
	unsigned int n = strlen(s);

	if ( do_indent && IsReadable() && offset > 0 &&
	     ((const char*) base)[offset - 1] == '\n' )
		Indent();

	if ( IsBinary() )
		AddBytes(s, n+1);
	else
		AddBytes(s, n);
	}

void ODesc::Add(int i)
	{
	if ( IsBinary() )
		AddBytes(&i, sizeof(i));
	else
		{
		char tmp[256];
		sprintf(tmp, "%d", i);
		Add(tmp);
		}
	}

void ODesc::Add(uint32 u)
	{
	if ( IsBinary() )
		AddBytes(&u, sizeof(u));
	else
		{
		char tmp[256];
		sprintf(tmp, "%u", u);
		Add(tmp);
		}
	}

void ODesc::Add(int64 i)
	{
	if ( IsBinary() )
		AddBytes(&i, sizeof(i));
	else
		{
		char tmp[256];
		sprintf(tmp, "%" PRId64, i);
		Add(tmp);
		}
	}

void ODesc::Add(uint64 u)
	{
	if ( IsBinary() )
		AddBytes(&u, sizeof(u));
	else
		{
		char tmp[256];
		sprintf(tmp, "%" PRIu64, u);
		Add(tmp);
		}
	}

void ODesc::Add(double d)
	{
	if ( IsBinary() )
		AddBytes(&d, sizeof(d));
	else
		{
		char tmp[256];
		sprintf(tmp, IsReadable() ? "%.15g" : "%.17g", d);
		Add(tmp);

		if ( d == double(int(d)) )
			// disambiguate from integer
			Add(".0");
		}
	}

void ODesc::AddCS(const char* s)
	{
	int n = strlen(s);
	Add(n);
	if ( ! IsBinary() )
		Add(" ");
	Add(s);
	}

void ODesc::AddBytes(const BroString* s)
	{
	if ( IsReadable() )
		{
		int render_style = BroString::EXPANDED_STRING;
		if ( Style() == ALTERNATIVE_STYLE )
			// Only change NULs, since we can't in any case
			// cope with them.
			render_style = BroString::ESC_NULL;

		const char* str = s->Render(render_style);
		Add(str);
		delete [] str;
		}
	else
		{
		Add(s->Len());
		if ( ! IsBinary() )
			Add(" ");
		AddBytes(s->Bytes(), s->Len());
		}
	}

void ODesc::Indent()
	{
	if ( indent_with_spaces > 0 )
		{
		for ( int i = 0; i < indent_level; ++i )
			for ( int j = 0; j < indent_with_spaces; ++j )
				Add(" ", 0);
		}
	else
		{
		for ( int i = 0; i < indent_level; ++i )
			Add("\t", 0);
		}
	}

static const char hex_chars[] = "0123456789abcdef";

static const char* find_first_unprintable(ODesc* d, const char* bytes, unsigned int n)
	{
	if ( d->IsBinary() )
		return 0;

	while ( n-- )
		{
		if ( ! isprint(*bytes) )
			return bytes;
		++bytes;
		}

	return 0;
	}

void ODesc::AddBytes(const void* bytes, unsigned int n)
	{
	const char* s = (const char*) bytes;
	const char* e = (const char*) bytes + n;

	while ( s < e )
		{
		const char* t1 = escape ? (const char*) memchr(s, escape[0], e - s) : e;
		const char* t2 = find_first_unprintable(this, s, t1 ? e - t1 : e - s);

		if ( t2 && (t2 < t1 || ! t1) )
			{
			AddBytesRaw(s, t2 - s);

			char hex[6] = "\\x00";
			hex[2] = hex_chars[((*t2) & 0xf0) >> 4];
			hex[3] = hex_chars[(*t2) & 0x0f];
			AddBytesRaw(hex, sizeof(hex));

			s = t2 + 1;
			continue;
			}

		if ( ! escape )
			break;

		if ( ! t1 )
			break;

		if ( memcmp(t1, escape, escape_len) != 0 )
			break;

		AddBytesRaw(s, t1 - s);

		for ( int i = 0; i < escape_len; ++i )
			{
			char hex[5] = "\\x00";
			hex[2] = hex_chars[((*t1) & 0xf0) >> 4];
			hex[3] = hex_chars[(*t1) & 0x0f];
			AddBytesRaw(hex, sizeof(hex));
			++t1;
			}

		s = t1;
		}

	if ( s < e )
		AddBytesRaw(s, e - s);
	}

void ODesc::AddBytesRaw(const void* bytes, unsigned int n)
	{
	if ( n == 0 )
		return;

	if ( f )
		{
		static bool write_failed = false;

		if ( ! f->Write((const char*) bytes, n) )
			{
			if ( ! write_failed )
				// Most likely it's a "disk full" so report
				// subsequent failures only once.
				run_time(fmt("error writing to %s: %s", f->Name(), strerror(errno)));

			write_failed = true;
			return;
			}

		write_failed = false;
		}

	else
		{
		Grow(n);

		// The following casting contortions are necessary because
		// simply using &base[offset] generates complaints about
		// using a void* for pointer arithemtic.
		memcpy((void*) &((char*) base)[offset], bytes, n);
		offset += n;

		((char*) base)[offset] = '\0';	// ensure that always NUL-term.
		}
	}

void ODesc::Grow(unsigned int n)
	{
	while ( offset + n + SLOP >= size )
		{
		size *= 2;
		base = safe_realloc(base, size);
		if ( ! base )
			OutOfMemory();
		}
	}

void ODesc::OutOfMemory()
	{
	internal_error("out of memory");
	}
