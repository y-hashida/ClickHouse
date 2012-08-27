#pragma once

#include <string.h>

#include <DB/Columns/ColumnArray.h>
#include <DB/Columns/ColumnsNumber.h>


namespace DB
{

/** Cтолбeц значений типа "строка".
  * Отличается от массива UInt8 только получением элемента (в виде String, а не Array)
  */
class ColumnString : public ColumnArray
{
private:
	ColumnUInt8::Container_t & char_data;

public:
	/** Создать пустой столбец строк, с типом значений */
	ColumnString()
		: ColumnArray(new ColumnUInt8()),
		char_data(dynamic_cast<ColumnUInt8 &>(*data).getData())
	{
	}

	std::string getName() const { return "ColumnString"; }

	ColumnPtr cloneEmpty() const
	{
		return new ColumnString;
	}
	
	Field operator[](size_t n) const
	{
		size_t offset = n == 0 ? 0 : getOffsets()[n - 1];
		size_t size = getOffsets()[n] - offset - 1;
		const char * s = reinterpret_cast<const char *>(&dynamic_cast<const ColumnUInt8 &>(*data).getData()[offset]);
		return String(s, size);
	}

	void insert(const Field & x)
	{
		const String & s = boost::get<const String &>(x);
		size_t old_size = char_data.size();
		size_t size_to_append = s.size() + 1;
		char_data.resize(old_size + size_to_append);
		memcpy(&char_data[old_size], s.c_str(), size_to_append);
		getOffsets().push_back((getOffsets().size() == 0 ? 0 : getOffsets().back()) + size_to_append);
	}

	void insertDefault()
	{
		char_data.push_back(0);
		getOffsets().push_back(getOffsets().size() == 0 ? 1 : (getOffsets().back() + 1));
	}

	int compareAt(size_t n, size_t m, const IColumn & rhs_) const
	{
		const ColumnString & rhs = static_cast<const ColumnString &>(rhs_);

		/** Для производительности, строки сравниваются до первого нулевого байта.
		  * (если нулевой байт в середине строки, то то, что после него - игнорируется)
		  * Замечу, что завершающий нулевой байт всегда есть.
		  */
		return strcmp(
			reinterpret_cast<const char *>(&char_data[offsetAt(n)]),
			reinterpret_cast<const char *>(&rhs.char_data[rhs.offsetAt(m)]));
	}

	struct less
	{
		const ColumnString & parent;
		less(const ColumnString & parent_) : parent(parent_) {}
		bool operator()(size_t lhs, size_t rhs) const
		{
			return 0 > strcmp(
				reinterpret_cast<const char *>(&parent.char_data[parent.offsetAt(lhs)]),
				reinterpret_cast<const char *>(&parent.char_data[parent.offsetAt(rhs)]));
		}
	};

	Permutation getPermutation() const
	{
		size_t s = getOffsets().size();
		Permutation res(s);
		for (size_t i = 0; i < s; ++i)
			res[i] = i;

		std::sort(res.begin(), res.end(), less(*this));
		return res;
	}

	void replicate(const Offsets_t & replicate_offsets)
	{
		size_t col_size = size();
		if (col_size != replicate_offsets.size())
			throw Exception("Size of offsets doesn't match size of column.", ErrorCodes::SIZES_OF_COLUMNS_DOESNT_MATCH);

		ColumnUInt8::Container_t tmp_chars;
		Offsets_t tmp_offsets;
		tmp_chars.reserve(char_data.size() / col_size * replicate_offsets.back());
		tmp_offsets.reserve(replicate_offsets.back());

		Offset_t prev_replicate_offset = 0;
		Offset_t prev_string_offset = 0;
		Offset_t current_new_offset = 0;

		for (size_t i = 0; i < col_size; ++i)
		{
			size_t size_to_replicate = replicate_offsets[i] - prev_replicate_offset;
			size_t string_size = getOffsets()[i] - prev_string_offset;

			for (size_t j = 0; j < size_to_replicate; ++j)
			{
				current_new_offset += string_size;
				tmp_offsets.push_back(current_new_offset);
				
				for (size_t k = 0; k < string_size; ++k)
					tmp_chars.push_back(char_data[prev_string_offset + k]);
			}

			prev_replicate_offset = replicate_offsets[i];
			prev_string_offset = getOffsets()[i];
		}

		tmp_chars.swap(char_data);
		tmp_offsets.swap(getOffsets());
	}
};


}
