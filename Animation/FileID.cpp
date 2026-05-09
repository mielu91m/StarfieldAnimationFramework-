#include "FileID.h"
#include "Util/String.h"

namespace Animation
{
	FileID::FileID(std::string_view a_filePath, std::string_view a_id)
		: _filePath(Util::String::ToLower(a_filePath))
		, _id(Util::String::ToLower(a_id))
	{}

	std::string_view FileID::QPath() const { return _filePath; }
	std::string_view FileID::QID() const { return _id; }

	bool FileID::operator==(const FileID& a_rhs) const
	{
		return _filePath == a_rhs._filePath && _id == a_rhs._id;
	}

	bool FileID::operator<(const FileID& a_rhs) const
	{
		return _filePath < a_rhs._filePath || (_filePath == a_rhs._filePath && _id < a_rhs._id);
	}

	bool AnimID::operator==(const AnimID& other) const
	{
		return file == other.file && skeleton == other.skeleton;
	}

	bool AnimID::operator<(const AnimID& other) const
	{
		return file < other.file || (file == other.file && skeleton < other.skeleton);
	}
}
