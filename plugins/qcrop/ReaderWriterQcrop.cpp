/**********************************************************************
*
*    FILE:            ReaderWriterQcrop.cpp
*
*    DESCRIPTION:    helper class for reading a quarter tile of an image
*
*    CREATED BY:     Laurens Voerman, l.voerman@rug.nl
*
*    HISTORY:        Created   23.06.2026
*
*
**********************************************************************/

#include <osgDB/FileNameUtils>
#include <osgDB/FileUtils>
#include <osgDB/ReadFile>

#include <osg/Texture2D>
#include <osg/PagedLOD>
#include <osg/PatchParameter>
#include <osg/ProxyNode>
#include <osg/MatrixTransform>
#include <iomanip>

#ifndef WIN32
#include <unistd.h> //for g++ 4.7
#endif

#define EXTENSION_NAME "qcrop"
static bool getFilenameAndParams(const std::string& input, std::string& filename, std::string& params)
{
	// find the start of the params list, accounting for nesting of [] and () brackets,
	// note, we are working backwards.
	int noNestedBrackets = 0;
	std::string::size_type pos = input.size();
	for (; pos > 0; )
	{
		--pos;
		char c = input[pos];
		if (c == ']') ++noNestedBrackets;
		else if (c == '[') --noNestedBrackets;
		else if (c == ')') ++noNestedBrackets;
		else if (c == '(') --noNestedBrackets;
		else if (c == '.' && noNestedBrackets == 0) break;
	}

	// get the next "extension", which actually contains the pseudo-loader parameters
	params = input.substr(pos + 1, std::string::npos);
	if (params.empty())
	{
		OSG_WARN << "Missing parameters for " EXTENSION_NAME " pseudo-loader" << std::endl;
		return false;
	}

	// clear the params sting of any brackets.
	std::string::size_type params_pos = params.size();
	for (; params_pos > 0; )
	{
		--params_pos;
		char c = params[params_pos];
		if (c == ']' || c == '[' || c == ')' || c == '(')
		{
			params.erase(params_pos, 1);
		}
	}

	// strip the "params extension", which must leave a sub-filename.
	filename = input.substr(0, pos);

	return true;
}

osg::Image* cropImage(int width, int height, int depth, int s_offset, int t_offset, int r_offset, const osg::Image* source) {
	if (!source) return NULL;
	int src_s = source->s();
	int src_t = source->t();
	int src_r = source->r();
	GLenum pixelFormat = source->getPixelFormat();
	GLenum type = source->getDataType();
	osg::Vec3i footprint = osg::Image::computeBlockFootprint(pixelFormat);
	if ((s_offset % footprint.x()) || (t_offset % footprint.y()) || (r_offset % footprint.z())) {
		OSG_WARN << "Error cropImage() did not succeed : offset " << s_offset << "," << t_offset << "," << r_offset << " not multiple of footprint " << footprint.x() << "," << footprint.y() << "," << footprint.z() << std::endl;
		return NULL;
	}
	osg::Image* target = new osg::Image();
	target->allocateImage(width,  height,  depth, pixelFormat, type);

	unsigned int src_rowWidthInBlocks = (src_s + footprint.x() - 1) / footprint.x();
	unsigned int src_colHeightInBlocks = (src_t + footprint.y() - 1) / footprint.y();
	unsigned int src_rowStartInBlocks = s_offset / footprint.x(); // == (s_offset + footprint.x() - 1) / footprint.x();
	if (src_rowStartInBlocks > src_rowWidthInBlocks) src_rowStartInBlocks = src_rowWidthInBlocks; //keep difference non negative
	unsigned int src_colStartInBlocks = t_offset / footprint.y(); // == (t_offset + footprint.y() - 1) / footprint.y();
	if (src_colStartInBlocks > src_rowWidthInBlocks) src_colStartInBlocks = src_rowWidthInBlocks; //keep difference non negative

	unsigned int dst_rowWidthInBlocks = (width + footprint.x() - 1) / footprint.x();
	unsigned int dst_colHeightInBlocks = (height + footprint.y() - 1) / footprint.y();

	unsigned int rowCopyInBlocks = std::min(dst_rowWidthInBlocks, src_rowWidthInBlocks - src_rowStartInBlocks);
	unsigned int colCopyInBlocks = std::min(dst_colHeightInBlocks, src_colHeightInBlocks - src_colStartInBlocks);

	int blockSizeInBytes = osg::Image::computeBlockSize(pixelFormat,0);
	if (blockSizeInBytes == 0) {
		unsigned int pixelSize = osg::Image::computePixelSizeInBits(pixelFormat, type);
		blockSizeInBytes = pixelSize / 8;
	}

	unsigned int srcRowStepinBytes = src_rowWidthInBlocks * blockSizeInBytes;//mind packing
	unsigned int packing = source->getPacking();
	if (packing > 1) {
		unsigned int padding = srcRowStepinBytes % packing;
		srcRowStepinBytes += padding;
	}
	unsigned int dstRowStepinBytes = dst_rowWidthInBlocks * blockSizeInBytes;//packing = 1
	const unsigned char* data_src = source->data() + blockSizeInBytes * src_rowStartInBlocks + srcRowStepinBytes * src_colStartInBlocks;
	unsigned char* data_dst = target->data();
	unsigned int imgCopyInBlocks = depth - r_offset;
	if (r_offset > 0) {
		data_src += r_offset * source->getImageSizeInBytes();
	}
	//might not work if footprint.z() == 4 (dxt compressed slabs)
	for (unsigned int img = 0; img < imgCopyInBlocks; img += 1) {
		unsigned int slabsize = osg::minimum(footprint.z(), (int)(imgCopyInBlocks - img));

		for (unsigned int row = 0; row < colCopyInBlocks; row += 1) { //copy blocks in a row, footprint.y() rows at a time
			memcpy(data_dst, data_src, rowCopyInBlocks * blockSizeInBytes * slabsize);
			data_src += srcRowStepinBytes * slabsize;
			data_dst += dstRowStepinBytes * slabsize;
		}
		data_src += srcRowStepinBytes * slabsize * (src_colHeightInBlocks - colCopyInBlocks);
	}
	return target;
}

class ReaderWriterQcrop : public osgDB::ReaderWriter
{
public:

	ReaderWriterQcrop()
	{		
        supportsExtension(EXTENSION_NAME,"quarter image pseudo format");
    }

    virtual const char* className() const
    {
        return "qcrop - quarter image pseudo format";
    }

    virtual ReadResult readObject(const std::string& file, const osgDB::ReaderWriter::Options* options) const
    {
        return readImage(file,options);
    }


	virtual ReadResult readImage(const std::string& fileName, const osgDB::ReaderWriter::Options* options) const
	{
		std::string ext = osgDB::getLowerCaseFileExtension(fileName);
		if (!acceptsExtension(ext))
			return ReadResult::FILE_NOT_HANDLED;

		//OSG_INFO << "ReaderWriterQcrop( \"" << fileName << "\" )" << std::endl;

		// strip the pseudo-loader extension
		std::string tmpName = osgDB::getNameLessExtension(fileName);

		if (tmpName.empty())
			return ReadResult::FILE_NOT_HANDLED;

		std::string subFileName, params;
		if (!getFilenameAndParams(tmpName, subFileName, params))
		{
			return ReadResult::FILE_NOT_HANDLED;
		}

		if (subFileName.empty())
		{
			OSG_WARN << "Missing subfilename for " EXTENSION_NAME " pseudo-loader" << std::endl;
			return ReadResult::FILE_NOT_HANDLED;
		}

		//OSG_INFO << " params = \"" << params << "\"" << std::endl;
		//OSG_INFO << " subFileName = \"" << subFileName << "\"" << std::endl;
		osg::ref_ptr<osg::Image> subFile = osgDB::readRefImageFile(subFileName, options);
		if (!subFile.valid()) return ReadResult::FILE_NOT_FOUND;
		bool left = (params.find('l') != std::string::npos);
		bool right = (params.find('r') != std::string::npos);
		bool top = (params.find('t') != std::string::npos);
		bool bottom = (params.find('b') != std::string::npos);
		int width = subFile->s(), height = subFile->t(), depth = subFile->r();
		int s_offset = 0, t_offset = 0, r_offset = 0;
		if (left) {
			width -= width / 2;
		} else {
			if (right) {
				s_offset = width - width / 2;
				width = width / 2;
			}
		}
		if (bottom) {
			height -= height / 2;
		} else {
			if (top) {
				t_offset = height - height / 2;
				height = height / 2;
			}
		}
		return  cropImage(width, height, 1, s_offset, t_offset, 0, subFile.get());
	}
};

// now register with Registry to instantiate the above
// reader/writer.
REGISTER_OSGPLUGIN(qcrop, ReaderWriterQcrop)
