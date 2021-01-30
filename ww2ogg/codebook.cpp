#define __STDC_CONSTANT_MACROS
#include "codebook.h"

codebook_library::codebook_library(void)
    : codebook_data(NULL), codebook_offsets(NULL), codebook_count(0)
{ }

codebook_library::codebook_library(const string& filename, istream& is)
    : codebook_data(NULL), codebook_offsets(NULL), codebook_count(0)
{
    is.seekg(0, ios_base::end);
    size_t file_size = is.tellg();

    is.seekg(file_size-4, ios::beg);
    size_t offset_offset = Read32BitLittleEndian(is);
    size_t c_count = (file_size - offset_offset) / 4;
    codebook_count = static_cast<long>(c_count);

    codebook_data = new char [offset_offset];
    codebook_offsets = new long [codebook_count];

    is.seekg(0, ios::beg);
    for (size_t i = 0; i < offset_offset; i++)
    {
        codebook_data[i] = is.get();
    }

    for (size_t i = 0; i < codebook_count; i++)
    {
        codebook_offsets[i] = Read32BitLittleEndian(is);
    }
}

void codebook_library::rebuild(int i, OggBitStream& bos)
{
    const char * cb = get_codebook(i);
    unsigned long cb_size;

    {
        long signed_cb_size = get_codebook_size(i);

        if (!cb || -1 == signed_cb_size) throw Invalid_id(i);

        cb_size = signed_cb_size;
    }

    ArrayStreamBuffer asb(cb, cb_size);
    istream is(&asb);
    BitStream bis(is);

    rebuild(bis, cb_size, bos);
}

/* cb_size == 0 to not check size (for an inline bitstream) */
void codebook_library::copy(BitStream &bis, OggBitStream& bos)
{
    /* IN: 24 bit identifier, 16 bit dimensions, 24 bit entry count */

    ArbitraryPrecisionUnsignedInt<24> id;
    ArbitraryPrecisionUnsignedInt<16> dimensions;
    ArbitraryPrecisionUnsignedInt<24> entries;

    bis >> id >> dimensions >> entries;

    if (0x564342 != id)
    {
        throw Parse_error_str("invalid codebook identifier");
    }

    //cout << "Codebook with " << dimensions << " dimensions, " << entries << " entries" << endl;

    /* OUT: 24 bit identifier, 16 bit dimensions, 24 bit entry count */
    bos << id << ArbitraryPrecisionUnsignedInt<16>(dimensions) << ArbitraryPrecisionUnsignedInt<24>(entries);

    // gather codeword lengths

    /* IN/OUT: 1 bit ordered flag */
    ArbitraryPrecisionUnsignedInt<1> ordered;
    bis >> ordered;
    bos << ordered;
    if (ordered)
    {
        //cout << "Ordered " << endl;

        /* IN/OUT: 5 bit initial length */
        ArbitraryPrecisionUnsignedInt<5> initial_length;
        bis >> initial_length;
        bos << initial_length;

        unsigned int current_entry = 0;
        while (current_entry < entries)
        {
            /* IN/OUT: ilog(entries-current_entry) bit count w/ given length */
            ArbitraryPrecisionUnsignedInt number(ilog(entries-current_entry));
            bis >> number;
            bos << number;
            current_entry += number;
        }
        if (current_entry > entries) throw Parse_error_str("current_entry out of range");
    }
    else
    {
        /* IN/OUT: 1 bit sparse flag */
        ArbitraryPrecisionUnsignedInt<1> sparse;
        bis >> sparse;
        bos << sparse;

        //cout << "Unordered, ";

        //if (sparse)
        //{
        //    cout << "Sparse" << endl;
        //}
        //else
        //{
        //    cout << "Nonsparse" << endl;
        //}

        for (unsigned int i = 0; i < entries; i++)
        {
            bool present_bool = true;

            if (sparse)
            {
                /* IN/OUT 1 bit sparse presence flag */
                ArbitraryPrecisionUnsignedInt<1> present;
                bis >> present;
                bos << present;

                present_bool = (0 != present);
            }

            if (present_bool)
            {
                /* IN/OUT: 5 bit codeword length-1 */
                ArbitraryPrecisionUnsignedInt<5> codeword_length;
                bis >> codeword_length;
                bos << codeword_length;
            }
        }
    } // done with lengths


    // lookup table

    /* IN/OUT: 4 bit lookup type */
    ArbitraryPrecisionUnsignedInt<4> lookup_type;
    bis >> lookup_type;
    bos << lookup_type;

    if (0 == lookup_type)
    {
        //cout << "no lookup table" << endl;
    }
    else if (1 == lookup_type)
    {
        //cout << "lookup type 1" << endl;

        /* IN/OUT: 32 bit minimum length, 32 bit maximum length, 4 bit value length-1, 1 bit sequence flag */
        ArbitraryPrecisionUnsignedInt<32> min, max;
        ArbitraryPrecisionUnsignedInt<4> value_length;
        ArbitraryPrecisionUnsignedInt<1> sequence_flag;
        bis >> min >> max >> value_length >> sequence_flag;
        bos << min << max << value_length << sequence_flag;

        unsigned int quantvals = _book_maptype1_quantvals(entries, dimensions);
        for (unsigned int i = 0; i < quantvals; i++)
        {
            /* IN/OUT: n bit value */
            ArbitraryPrecisionUnsignedInt val(value_length+1);
            bis >> val;
            bos << val;
        }
    }
    else if (2 == lookup_type)
    {
        throw Parse_error_str("didn't expect lookup type 2");
    }
    else
    {
        throw Parse_error_str("invalid lookup type");
    }

    //cout << "total bits read = " << bis.get_total_bits_read() << endl;
}

/* cb_size == 0 to not check size (for an inline bitstream) */
void codebook_library::rebuild(BitStream &bis, unsigned long cb_size, OggBitStream& bos)
{
    /* IN: 4 bit dimensions, 14 bit entry count */

    ArbitraryPrecisionUnsignedInt<4> dimensions;
    ArbitraryPrecisionUnsignedInt<14> entries;

    bis >> dimensions >> entries;

    //cout << "Codebook " << i << ", " << dimensions << " dimensions, " << entries << " entries" << endl;
    //cout << "Codebook with " << dimensions << " dimensions, " << entries << " entries" << endl;

    /* OUT: 24 bit identifier, 16 bit dimensions, 24 bit entry count */
    bos << ArbitraryPrecisionUnsignedInt<24>(0x564342) << ArbitraryPrecisionUnsignedInt<16>(dimensions) << ArbitraryPrecisionUnsignedInt<24>(entries);

    // gather codeword lengths

    /* IN/OUT: 1 bit ordered flag */
    ArbitraryPrecisionUnsignedInt<1> ordered;
    bis >> ordered;
    bos << ordered;
    if (ordered)
    {
        //cout << "Ordered " << endl;

        /* IN/OUT: 5 bit initial length */
        ArbitraryPrecisionUnsignedInt<5> initial_length;
        bis >> initial_length;
        bos << initial_length;

        unsigned int current_entry = 0;
        while (current_entry < entries)
        {
            /* IN/OUT: ilog(entries-current_entry) bit count w/ given length */
            ArbitraryPrecisionUnsignedInt number(ilog(entries-current_entry));
            bis >> number;
            bos << number;
            current_entry += number;
        }
        if (current_entry > entries) throw Parse_error_str("current_entry out of range");
    }
    else
    {
        /* IN: 3 bit codeword length length, 1 bit sparse flag */
        ArbitraryPrecisionUnsignedInt<3> codeword_length_length;
        ArbitraryPrecisionUnsignedInt<1> sparse;
        bis >> codeword_length_length >> sparse;

        //cout << "Unordered, " << codeword_length_length << " bit lengths, ";

        if (0 == codeword_length_length || 5 < codeword_length_length)
        {
            throw Parse_error_str("nonsense codeword length");
        }

        /* OUT: 1 bit sparse flag */
        bos << sparse;
        //if (sparse)
        //{
        //    cout << "Sparse" << endl;
        //}
        //else
        //{
        //    cout << "Nonsparse" << endl;
        //}

        for (unsigned int i = 0; i < entries; i++)
        {
            bool present_bool = true;

            if (sparse)
            {
                /* IN/OUT 1 bit sparse presence flag */
                ArbitraryPrecisionUnsignedInt<1> present;
                bis >> present;
                bos << present;

                present_bool = (0 != present);
            }

            if (present_bool)
            {
                /* IN: n bit codeword length-1 */
                ArbitraryPrecisionUnsignedInt codeword_length(codeword_length_length);
                bis >> codeword_length;

                /* OUT: 5 bit codeword length-1 */
                bos << ArbitraryPrecisionUnsignedInt<5>(codeword_length);
            }
        }
    } // done with lengths


    // lookup table

    /* IN: 1 bit lookup type */
    ArbitraryPrecisionUnsignedInt<1> lookup_type;
    bis >> lookup_type;
    /* OUT: 4 bit lookup type */
    bos << ArbitraryPrecisionUnsignedInt<4>(lookup_type);

    if (0 == lookup_type)
    {
        //cout << "no lookup table" << endl;
    }
    else if (1 == lookup_type)
    {
        //cout << "lookup type 1" << endl;

        /* IN/OUT: 32 bit minimum length, 32 bit maximum length, 4 bit value length-1, 1 bit sequence flag */
        ArbitraryPrecisionUnsignedInt<32> min, max;
        ArbitraryPrecisionUnsignedInt<4> value_length;
        ArbitraryPrecisionUnsignedInt<1> sequence_flag;
        bis >> min >> max >> value_length >> sequence_flag;
        bos << min << max << value_length << sequence_flag;

        unsigned int quantvals = _book_maptype1_quantvals(entries, dimensions);
        for (unsigned int i = 0; i < quantvals; i++)
        {
            /* IN/OUT: n bit value */
            ArbitraryPrecisionUnsignedInt val(value_length+1);
            bis >> val;
            bos << val;
        }
    }
    else if (2 == lookup_type)
    {
        throw Parse_error_str("didn't expect lookup type 2");
    }
    else
    {
        throw Parse_error_str("invalid lookup type");
    }

    //cout << "total bits read = " << bis.get_total_bits_read() << endl;

    /* check that we used exactly all bytes */
    /* note: if all bits are used in the last byte there will be one extra 0 byte */
    if ( 0 != cb_size && bis.get_total_bits_read()/8+1 != cb_size )
    {
        throw Size_mismatch( cb_size, bis.get_total_bits_read()/8+1 );
    }
}
