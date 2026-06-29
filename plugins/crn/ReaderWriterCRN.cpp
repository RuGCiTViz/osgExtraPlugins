/**********************************************************************
*
*    FILE:            ReaderWriterCRN.cpp
*
*    DESCRIPTION:    Class for reading a CRN file into an osg::Image.
*					 by transcoding to dds
*
*                    based on crunch; homepage at
*					 https://code.google.com/p/crunch/
*                    version crunch_v101_r1.7z (Apr 2012)
*
*    CREATED BY:     Laurens Voerman, l.voerman@rug.nl
*
*    HISTORY:        Created   04.06.2014
*
*    WARNING:
*          Write function very crude:
*				- write png file
*				- call system to run something called "cruch" - hoping it will find the executable from the crunch package in the path
*				- delete the png file
*
**********************************************************************/

#include <osgDB/FileNameUtils>
#include <osgDB/FileUtils>
#include <osg/Texture2D>
#include <osg/LOD>
#include <osg/PagedLOD>
#include <osg/ProxyNode>
#include <osg/MatrixTransform>
#include <iomanip>

#ifndef WIN32
#include <unistd.h> //for g++ 4.7
#endif

#define PLATFORM_NACL
// CRN transcoder library.
#include "inc/crn_decomp.h"
// .DDS file format definitions.
#include "inc/dds_defs.h"

//// helper to create istream from memory buffer
struct membuf : std::streambuf {
	membuf(char const* base, size_t size) {
		char* p(const_cast<char*>(base));
		this->setg(p, p, p + size);
	}
	std::streampos seekoff(std::streamoff off, std::ios_base::seekdir way,
		std::ios_base::openmode which = std::ios_base::in | std::ios_base::out) {
		gbump(off);
		return (std::streampos(0));
	};
};
struct imemstream : virtual membuf, std::istream {
	imemstream(char const* base, size_t size)
		: membuf(base, size)
		, std::istream(static_cast<std::streambuf*>(this)) {
	}
};

struct subTile {
	subTile(float xMin, float xMax, float yMin, float yMax,const std::string &fileName) :
		_xMin(xMin),
		_xMax(xMax),
		_yMin(yMin),
		_yMax(yMax),
		_fileName(fileName) {}
	float _xMin;
	float _xMax;
	float _yMin;
	float _yMax;
	std::string _fileName;
};
osgDB::ReaderWriter::ReadResult ReadCRNFile(std::istream& _istream, unsigned int _numMipmapsToSkip, const osgDB::ReaderWriter::Options* opt)
{
	osgDB::ReaderWriter *rw = osgDB::Registry::instance()->getReaderWriterForExtension("dds");
	if (!rw) return osgDB::ReaderWriter::ReadResult::ERROR_IN_READING_FILE;

	_istream.seekg(0, std::ios::end);
	unsigned int fileSize = _istream.tellg();
	unsigned int fileBytesRead = 0;
	_istream.seekg(0, std::ios::beg);
	if (fileSize <= 0) {
		return osgDB::ReaderWriter::ReadResult::ERROR_IN_READING_FILE;
	}
	char* imageData = new char[fileSize];
	if (!imageData)
	{
		OSG_WARN << "ReadCRNFile warning: imageData == NULL" << std::endl;
		return osgDB::ReaderWriter::ReadResult::ERROR_IN_READING_FILE;
	}
	if (_numMipmapsToSkip == 0) {
		// Load the source file into memory.
		if (!_istream.read((char*)imageData, fileSize))
		{
			delete[] imageData;
			OSG_WARN << "ReadCRNFile warning: couldn't read imageData" << std::endl;
			return osgDB::ReaderWriter::ReadResult::ERROR_IN_READING_FILE;
		}
	} else {
		// Load the source file into memory.
		fileBytesRead = sizeof(crnd::crn_header);
		if ((fileSize > 4096) && (sizeof(crnd::crn_header) < 4096)) fileBytesRead = 4096;
		if ((fileSize < sizeof(crnd::crn_header)) || (!_istream.read((char*)imageData, fileBytesRead)))
		{
			delete[] imageData;
			OSG_WARN << "ReadCRNFile warning: couldn't read image header." << std::endl;
			return osgDB::ReaderWriter::ReadResult::ERROR_IN_READING_FILE;
		}
		crnd::crn_header tmp_header;//unused
		const crnd::crn_header* header = crnd::crnd_get_header(tmp_header, imageData, fileSize);
//		header->m_level_ofs[0];
		unsigned int bytesToRead = header->m_level_ofs[0] - fileBytesRead;
		if ((!_istream.read((char*)(imageData + fileBytesRead), bytesToRead)))
		{
			delete[] imageData;
			OSG_WARN << "ReadCRNFile warning: couldn't read image data." << std::endl;
			return osgDB::ReaderWriter::ReadResult::ERROR_IN_READING_FILE;
		}
		fileBytesRead += bytesToRead;
		//now skip the unwanted mipmaps
		unsigned int crn_levels = header->m_levels;
		if (_numMipmapsToSkip >= crn_levels) {
			OSG_WARN << "ReadCRNFile warning: couldn't skip " << _numMipmapsToSkip << " levels; " << " only " << crn_levels - 1 << " skipped (to last level in file)." <<std::endl;
			_numMipmapsToSkip = crn_levels - 1;
		}
		unsigned int bytesToSkip = header->m_level_ofs[_numMipmapsToSkip] - fileBytesRead;
		if ((!_istream.seekg(bytesToSkip,std::ios::cur)))
		{
			delete[] imageData;
			OSG_WARN << "ReadCRNFile warning: couldn't skip image data." << std::endl;
			return osgDB::ReaderWriter::ReadResult::ERROR_IN_READING_FILE;
		}
		fileBytesRead += bytesToSkip;//well, not actually read
		//read the rest of the file
		bytesToRead = fileSize - fileBytesRead;
		if ((!_istream.read((char*)(imageData + fileBytesRead), bytesToRead)))
		{
			delete[] imageData;
			OSG_WARN << "ReadCRNFile warning: couldn't read image mipmap data." << std::endl;
			return osgDB::ReaderWriter::ReadResult::ERROR_IN_READING_FILE;
		}
	}
	crnd::crn_texture_info tex_info;
	if (!crnd::crnd_get_texture_info(imageData, fileSize, &tex_info))
	{
		delete[] imageData;
		OSG_WARN << "ReadCRNFile error: crnd_get_texture_info() failed!" << std::endl;
		return osgDB::ReaderWriter::ReadResult::ERROR_IN_READING_FILE;
	}
	crnd::crnd_unpack_context pContext = crnd::crnd_unpack_begin(imageData, fileSize);
	if (!pContext)
	{
		delete[] imageData;
		OSG_WARN << "ReadCRNFile error: crnd_unpack_begin() failed!" << std::endl;
		return osgDB::ReaderWriter::ReadResult::ERROR_IN_READING_FILE;
	}

////////////////////////////////////////////
	// Prepare the DDS header.
	crnlib::DDSURFACEDESC2 dds_desc;
	memset(&dds_desc, 0, sizeof(dds_desc));
	dds_desc.dwSize = sizeof(dds_desc);
	dds_desc.dwFlags = crnlib::DDSD_CAPS | crnlib::DDSD_HEIGHT | crnlib::DDSD_WIDTH | crnlib::DDSD_PIXELFORMAT | ((tex_info.m_levels > 1) ? crnlib::DDSD_MIPMAPCOUNT : 0);
	dds_desc.dwWidth = tex_info.m_width;
	dds_desc.dwHeight = tex_info.m_height;
//	unsigned int mipmaps = tex_info.m_levels - _numMipmapsToSkip;
	dds_desc.dwMipMapCount = (tex_info.m_levels > 1) ? tex_info.m_levels : 0;

	dds_desc.ddpfPixelFormat.dwSize = sizeof(crnlib::DDPIXELFORMAT);
	dds_desc.ddpfPixelFormat.dwFlags = crnlib::DDPF_FOURCC;
	crn_format fundamental_fmt = crnd::crnd_get_fundamental_dxt_format(tex_info.m_format);
	dds_desc.ddpfPixelFormat.dwFourCC = crnd::crnd_crn_format_to_fourcc(fundamental_fmt);
	if (fundamental_fmt != tex_info.m_format)
	{
		// It's a funky swizzled DXTn format - write its FOURCC to dwRGBBitCount.
		dds_desc.ddpfPixelFormat.dwRGBBitCount = crnd::crnd_crn_format_to_fourcc(tex_info.m_format);
	}

	dds_desc.ddsCaps.dwCaps = crnlib::DDSCAPS_TEXTURE;
	if (tex_info.m_levels > 1)
	{
		dds_desc.ddsCaps.dwCaps |= (crnlib::DDSCAPS_COMPLEX | crnlib::DDSCAPS_MIPMAP);
	}

	if (tex_info.m_faces == 6)
	{
		dds_desc.ddsCaps.dwCaps2 = crnlib::DDSCAPS2_CUBEMAP |
			crnlib::DDSCAPS2_CUBEMAP_POSITIVEX | crnlib::DDSCAPS2_CUBEMAP_NEGATIVEX | crnlib::DDSCAPS2_CUBEMAP_POSITIVEY |
			crnlib::DDSCAPS2_CUBEMAP_NEGATIVEY | crnlib::DDSCAPS2_CUBEMAP_POSITIVEZ | crnlib::DDSCAPS2_CUBEMAP_NEGATIVEZ;
	}

	// Set pitch/linearsize field (some DDS readers require this field to be non-zero).
	int bits_per_pixel = crnd::crnd_get_crn_format_bits_per_texel(tex_info.m_format);
	dds_desc.lPitch = (((dds_desc.dwWidth + 3) & ~3) * ((dds_desc.dwHeight + 3) & ~3) * bits_per_pixel) >> 3;
	dds_desc.dwFlags |= crnlib::DDSD_LINEARSIZE;

	// Now transcode all face and mipmap levels into memory, one mip level at a time.
	void *pImages[cCRNMaxFaces][cCRNMaxLevels];
	crn_uint32 image_size_in_bytes[cCRNMaxLevels];
	memset(pImages, 0, sizeof(pImages));
	memset(image_size_in_bytes, 0, sizeof(image_size_in_bytes));

	if (tex_info.m_faces == 1) { //copy directly to dds block (no need to rotate data)
		crn_uint32 row_pitch_in_bytes[cCRNMaxLevels];
		memset(row_pitch_in_bytes, 0, sizeof(row_pitch_in_bytes));
		crn_uint32 dds_size = sizeof(crnlib::cDDSFileSignature) + sizeof(dds_desc);//header
		for (crn_uint32 level_index = 0; level_index < tex_info.m_levels; level_index++)
		{
			// Compute the face's width, height, number of DXT blocks per row/col, etc.
			const crn_uint32 width = std::max(1U, tex_info.m_width >> level_index);
			const crn_uint32 height = std::max(1U, tex_info.m_height >> level_index);
			const crn_uint32 blocks_x = (width + 3) >> 2;
			const crn_uint32 blocks_y = (height + 3) >> 2;
			const crn_uint32 row_pitch = blocks_x * crnd::crnd_get_bytes_per_dxt_block(tex_info.m_format);
			const crn_uint32 total_face_size = row_pitch * blocks_y;

			image_size_in_bytes[level_index] = total_face_size;
			row_pitch_in_bytes[level_index] = row_pitch;
			dds_size += total_face_size;
		}
		//allocate mem block of dds_size
		char* ddsFile = new char[dds_size];
		if (!ddsFile) {
			delete[] imageData;
			OSG_WARN << "ReadCRNFile error: Out of memory" << std::endl;
			return osgDB::ReaderWriter::ReadResult::INSUFFICIENT_MEMORY_TO_LOAD;
		}
		memcpy(ddsFile, &crnlib::cDDSFileSignature, sizeof(crnlib::cDDSFileSignature));
		crn_uint32 ddsFileNextChar = sizeof(crnlib::cDDSFileSignature);
		memcpy(&ddsFile[ddsFileNextChar], &dds_desc, sizeof(dds_desc));
		ddsFileNextChar += sizeof(dds_desc);
		for (crn_uint32 level_index = 0; level_index < tex_info.m_levels; level_index++)
		{
			pImages[0][level_index] = &ddsFile[ddsFileNextChar];
			ddsFileNextChar += image_size_in_bytes[level_index];

			// Prepare the face pointer array needed by crnd_unpack_level().
			void *pDecomp_images[cCRNMaxFaces];
			pDecomp_images[0] = pImages[0][level_index];

			// Now transcode the level to raw DXTn
			if (level_index >= _numMipmapsToSkip)
				if (!crnd::crnd_unpack_level(pContext, pDecomp_images, image_size_in_bytes[level_index], row_pitch_in_bytes[level_index], level_index))
				{
					crnd::crnd_unpack_end(pContext);
					delete[] ddsFile;
					delete[] imageData;
					OSG_WARN << "ReadCRNFile error: crnd::crnd_unpack_level failed" << std::endl;
					return osgDB::ReaderWriter::ReadResult::ERROR_IN_READING_FILE;
				}
		}
		crnd::crnd_unpack_end(pContext);

		std::istream *dds_istream = new imemstream(ddsFile, dds_size);
		osgDB::ReaderWriter::ReadResult result = rw->readImage(*dds_istream, opt);
		delete(dds_istream);
		delete[] ddsFile;
		delete[] imageData;
		return result;
	}

	std::stringstream dds_strstream;
	// Write the 4-byte DDS signature
	dds_strstream.write((char *)&crnlib::cDDSFileSignature, sizeof(crnlib::cDDSFileSignature));

		// Write the DDS header to the output file.
	dds_strstream.write((char *)&dds_desc, sizeof(dds_desc));

	for (crn_uint32 level_index = 0; level_index < tex_info.m_levels; level_index++)
	{
		// Compute the face's width, height, number of DXT blocks per row/col, etc.
		const crn_uint32 width = std::max(1U, tex_info.m_width >> level_index);
		const crn_uint32 height = std::max(1U, tex_info.m_height >> level_index);
		const crn_uint32 blocks_x = std::max(1U, (width + 3) >> 2);
		const crn_uint32 blocks_y = std::max(1U, (height + 3) >> 2);
		const crn_uint32 row_pitch = blocks_x * crnd::crnd_get_bytes_per_dxt_block(tex_info.m_format);
		const crn_uint32 total_face_size = row_pitch * blocks_y;

		image_size_in_bytes[level_index] = total_face_size;

		for (crn_uint32 face_index = 0; face_index < tex_info.m_faces; face_index++)
		{
			void *p = malloc(total_face_size);
			if (!p)
			{
				for (crn_uint32 f = 0; f < cCRNMaxFaces; f++)
					for (crn_uint32 l = 0; l < cCRNMaxLevels; l++)
						free(pImages[f][l]);
				crnd::crnd_unpack_end(pContext);
				delete[] imageData;
				OSG_WARN << "ReadCRNFile error: Out of memory" << std::endl;
				return osgDB::ReaderWriter::ReadResult::INSUFFICIENT_MEMORY_TO_LOAD;
			}

			pImages[face_index][level_index] = p;
		}

		// Prepare the face pointer array needed by crnd_unpack_level().
		void *pDecomp_images[cCRNMaxFaces];
		for (crn_uint32 face_index = 0; face_index < tex_info.m_faces; face_index++)
			pDecomp_images[face_index] = pImages[face_index][level_index];

		// Now transcode the level to raw DXTn
		if (level_index >= _numMipmapsToSkip)
		if (!crnd::crnd_unpack_level(pContext, pDecomp_images, total_face_size, row_pitch, level_index))
		{
			for (crn_uint32 f = 0; f < cCRNMaxFaces; f++)
				for (crn_uint32 l = 0; l < cCRNMaxLevels; l++)
					free(pImages[f][l]);

			crnd::crnd_unpack_end(pContext);
			delete[] imageData;
			OSG_WARN << "ReadCRNFile error: crnd::crnd_unpack_level failed" << std::endl;
			return osgDB::ReaderWriter::ReadResult::ERROR_IN_READING_FILE;
		}
	}

	// Now write the DXTn data to the DDS file in face-major order.
	for (crn_uint32 face_index = 0; face_index < tex_info.m_faces; face_index++)
		for (crn_uint32 level_index = 0; level_index < tex_info.m_levels; level_index++)
			dds_strstream.write((char *)pImages[face_index][level_index], image_size_in_bytes[level_index]);
//	fwrite(pImages[face_index][level_index], image_size_in_bytes[level_index], 1, pDDS_file);

	for (crn_uint32 f = 0; f < cCRNMaxFaces; f++)
		for (crn_uint32 l = 0; l < cCRNMaxLevels; l++)
			free(pImages[f][l]);

	crnd::crnd_unpack_end(pContext);

	dds_strstream.seekg(0, std::ios::beg);//read from begin
	osgDB::ReaderWriter::ReadResult result = rw->readImage(dds_strstream, opt);

	delete[] imageData;
	return result;
}
bool WriteCRNFile(const osg::Image *img, std::ostream& fout, const osgDB::ReaderWriter::Options* opt)
{
	OSG_WARN << "WriteCRNFile error: not implemented yet!" << std::endl;
	return false;
}


class ReaderWriterCRN : public osgDB::ReaderWriter
{
public:

	ReaderWriterCRN()
    {
        supportsExtension("crn","CRN image format");
        supportsOption("dds_dxt1_rgb","Set the pixel format of DXT1 encoded images to be RGB variant of DXT1");
        supportsOption("dds_dxt1_rgba","Set the pixel format of DXT1 encoded images to be RGBA variant of DXT1");
        supportsOption("dds_dxt1_detect_rgba","For DXT1 encode images set the pixel format according to presence of transparent pixels");
        supportsOption("dds_flip","Flip the image about the horizontl axis");
    }

    virtual const char* className() const
    {
        return "CRN Image Reader/Writer";
    }

    virtual ReadResult readObject(const std::string& file, const osgDB::ReaderWriter::Options* options) const
    {
        return readNode(file,options);
    }

    virtual ReadResult readObject(std::istream& fin, const Options* options) const
    {
        return readImage(fin,options);
    }

    virtual ReadResult readImage(const std::string& file, const osgDB::ReaderWriter::Options* options) const
    {
        std::string ext = osgDB::getLowerCaseFileExtension(file);
        if (!acceptsExtension(ext)) return ReadResult::FILE_NOT_HANDLED;

        std::string fileName = osgDB::findDataFile( file, options );

        if (fileName.empty()) return ReadResult::FILE_NOT_FOUND;

        osgDB::ifstream stream(fileName.c_str(), std::ios::in | std::ios::binary);
        if(!stream) return ReadResult::FILE_NOT_HANDLED;
        ReadResult rr = readImage(stream, options);
        if(rr.validImage()) rr.getImage()->setFileName(file);
        return rr;
    }

    virtual ReadResult readImage(std::istream& fin, const Options* options) const
    {
		//read and sum up "mmToSkip="
		int mmToSkip = 0;
		if (options) {
			std::string::size_type opt = options->getOptionString().find("mmToSkip=");
			while (opt != std::string::npos) {
				mmToSkip += atoi(&options->getOptionString().c_str()[opt + 9]);
				opt = options->getOptionString().find("mmToSkip=", opt + sizeof("mmToSkip="));
			}
			if (mmToSkip < 0 ) mmToSkip = 0;
		}
		osgDB::ReaderWriter::ReadResult result = ReadCRNFile(fin, (unsigned int)mmToSkip, options);
		if (!result.success()) return result;
		osg::Image* osgImage = result.getImage();
        if (osgImage==NULL) return ReadResult::FILE_NOT_HANDLED;
		/* done by dds reader
        if (osgImage->getPixelFormat()==GL_COMPRESSED_RGB_S3TC_DXT1_EXT ||
            osgImage->getPixelFormat()==GL_COMPRESSED_RGBA_S3TC_DXT1_EXT)
        {
            if (options && options->getOptionString().find("dds_dxt1_rgba")!=std::string::npos)
            {
                osgImage->setPixelFormat(GL_COMPRESSED_RGBA_S3TC_DXT1_EXT);
                osgImage->setInternalTextureFormat(GL_COMPRESSED_RGBA_S3TC_DXT1_EXT);
            }
            else if (options && options->getOptionString().find("dds_dxt1_rgb")!=std::string::npos)
            {
                osgImage->setPixelFormat(GL_COMPRESSED_RGB_S3TC_DXT1_EXT);
                osgImage->setInternalTextureFormat(GL_COMPRESSED_RGB_S3TC_DXT1_EXT);
            }
            else if (options && options->getOptionString().find("dds_dxt1_detect_rgba")!=std::string::npos)
            {
                // check to see if DXT1c (RGB_S3TC_DXT1) format image might actually be
                // a DXT1a format image

                // temporarily set pixel format to GL_COMPRESSED_RGBA_S3TC_DXT1_EXT so
                // that the isImageTranslucent() method assumes that RGBA is present and then
                // checks the alpha values to see if they are all 1.0.
                osgImage->setPixelFormat(GL_COMPRESSED_RGBA_S3TC_DXT1_EXT);
                osgImage->setInternalTextureFormat(GL_COMPRESSED_RGBA_S3TC_DXT1_EXT);
                if (!osgImage->isImageTranslucent())
                {
                    // image contains alpha's that are 1.0, so treat is as RGB
                    OSG_INFO<<"Image with PixelFormat==GL_COMPRESSED_RGB_S3TC_DXT1_EXT is opaque."<<std::endl;
                    osgImage->setPixelFormat(GL_COMPRESSED_RGB_S3TC_DXT1_EXT);
                    osgImage->setInternalTextureFormat(GL_COMPRESSED_RGB_S3TC_DXT1_EXT);
                }
                else
                {
                    // image contains alpha's that are non 1.0, so treat is as RGBA
                    OSG_INFO<<"Image with PixelFormat==GL_COMPRESSED_RGB_S3TC_DXT1_EXT has transparency, setting format to GL_COMPRESSED_RGBA_S3TC_DXT1_EXT."<<std::endl;
                }
            }
        }

        if (options && options->getOptionString().find("dds_flip")!=std::string::npos)
        {
            osgImage->flipVertical();
        }
*/
        return osgImage;
    }

    virtual WriteResult writeObject(const osg::Object& object,const std::string& file, const osgDB::ReaderWriter::Options* options) const
    {
        const osg::Image* image = dynamic_cast<const osg::Image*>(&object);
        if (!image) return WriteResult::FILE_NOT_HANDLED;

        return writeImage(*image,file,options);
    }

    virtual WriteResult writeObject(const osg::Object& object,std::ostream& fout,const Options* options) const
    {
        const osg::Image* image = dynamic_cast<const osg::Image*>(&object);
        if (!image) return WriteResult::FILE_NOT_HANDLED;

        return writeImage(*image,fout,options);
    }


    virtual WriteResult writeImage(const osg::Image &image,const std::string& file, const osgDB::ReaderWriter::Options* options) const
    {
		std::string ext = osgDB::getFileExtension(file);
        if (!acceptsExtension(ext)) return WriteResult::FILE_NOT_HANDLED;
#if 1
		std::string pngfile(osgDB::getNameLessExtension(file));
		pngfile += std::string(".png");
		osgDB::ReaderWriter *rw = osgDB::Registry::instance()->getReaderWriterForExtension("png");
		WriteResult result = rw->writeImage(image, pngfile, options);
		if (result.success()) {
			//		std::string command("C:\\Users\\laurens\\Downloads\\crunch_v101_r1\\bin\\crunch_x64.exe");
			std::string command("crunch");
			command += std::string(" -file ");
			command += pngfile;
			command += std::string(" -outsamedir -dxt1 -quality 255");
			system(command.c_str());
#ifdef WIN32
			_unlink(pngfile.c_str());
#else
			unlink(pngfile.c_str());
#endif
		}
		return result;
#else
        osgDB::ofstream fout(file.c_str(), std::ios::out | std::ios::binary);
        if(!fout) return WriteResult::ERROR_IN_WRITING_FILE;

        return writeImage(image,fout,options);
#endif
    }

    virtual WriteResult writeImage(const osg::Image& image,std::ostream& fout,const Options* opt) const
    {
        bool success = WriteCRNFile(&image, fout, opt);

        if(success)
            return WriteResult::FILE_SAVED;
        else
            return WriteResult::ERROR_IN_WRITING_FILE;
    }
	virtual ReadResult readNode(const std::string& fileName, const osgDB::ReaderWriter::Options* options) const {
		std::string ext = osgDB::getFileExtension(fileName);
		if (!acceptsExtension(ext)) return ReadResult::FILE_NOT_HANDLED;
		std::string basename = osgDB::getSimpleFileName(fileName);
		std::string  charStr = "L";
		bool lodTile = (0==basename.substr(6, 1).compare(charStr));
		osg::MatrixTransform *matTrans = NULL;
		charStr = "C";
		bool cropTile = (0 == basename.substr(8, 1).compare(charStr));//15825798C023023.crn
		std::string subFileName = cropTile ? fileName.substr(0, fileName.length() - 11) + ".crn" : fileName;

		ReadResult img_rr = readImage(subFileName, options);
		osg::Image *img = img_rr.takeImage();
		std::vector<subTile> subTiles;
		int x, y, l, tileSize;
		l = 0;
		if (lodTile) {
			x = atoi(basename.substr(0, 3).c_str());
			y = atoi(basename.substr(3, 3).c_str());
			l = atoi(basename.substr(7, 1).c_str());
			tileSize = 1000 * (1 << l); // integer 2^l
			std::string f00, f01, f10, f11;
			if (l > 0) {
				int l2 = l - 1;
				int s2 = 1 << l2;
				std::string dirname = osgDB::getFilePath(fileName);
				if (!dirname.empty()) dirname += osgDB::getNativePathSeparator();
				std::stringstream t00; t00 << std::setw(3) << std::setfill('0') << x      << std::setw(3) << std::setfill('0') << y      << "L" << std::setw(1) << l2 << ".crn";
				std::stringstream t01; t01 << std::setw(3) << std::setfill('0') << x      << std::setw(3) << std::setfill('0') << y - s2 << "L" << std::setw(1) << l2 << ".crn";
				std::stringstream t10; t10 << std::setw(3) << std::setfill('0') << x + s2 << std::setw(3) << std::setfill('0') << y      << "L" << std::setw(1) << l2 << ".crn";
				std::stringstream t11; t11 << std::setw(3) << std::setfill('0') << x + s2 << std::setw(3) << std::setfill('0') << y - s2 << "L" << std::setw(1) << l2 << ".crn";
				f00 = osgDB::findDataFile(dirname + t00.str(), options);
				f01 = osgDB::findDataFile(dirname + t01.str(), options);
				f10 = osgDB::findDataFile(dirname + t10.str(), options);
				f11 = osgDB::findDataFile(dirname + t11.str(), options);
				if (l % 4 == 0) {
					if (f00.empty()) f00 = osgDB::findDataFile(dirname + basename.substr(0, 8) + osgDB::getNativePathSeparator() + t00.str(), options);
					if (f01.empty()) f01 = osgDB::findDataFile(dirname + basename.substr(0, 8) + osgDB::getNativePathSeparator() + t01.str(), options);
					if (f10.empty()) f10 = osgDB::findDataFile(dirname + basename.substr(0, 8) + osgDB::getNativePathSeparator() + t10.str(), options);
					if (f11.empty()) f11 = osgDB::findDataFile(dirname + basename.substr(0, 8) + osgDB::getNativePathSeparator() + t11.str(), options);
				}
				subTiles.emplace_back(0.0f, 0.5f, 0.5f, 1.0f, f00);
				subTiles.emplace_back(0.0f, 0.5f, 0.0f, 0.5f, f01);
				subTiles.emplace_back(0.5f, 1.0f, 0.5f, 1.0f, f10);
				subTiles.emplace_back(0.5f, 1.0f, 0.0f, 0.5f, f11);
			} else {//L0
				int hy = ((10 * y) / 3) * 3 + 2;//mod 3 + 2
				for (; hy > 10 * (y - 1); hy -= 3) {
					int topCrop = (hy - (10 * y)) > 0 ? hy - (10 * y) : 0;
					int bottomCrop = (((10 * y) - hy) > 7) ? 10 - ((10 * y) - hy) : 3;
					float rowMaxY = hy > (10 * y) ? 1.0f : 1.0f - (0.1f * ((10 * y) - hy));
					float rowMinY = ((10 * y) - (hy - 3)) > 10 ? 0.0f : 1.0f - (0.1f * ((10 * y) - (hy - 3)));

					int hx = (10 * x);
					bool fileExists[12];
					bool skip = false;
					for (int fileNo = 0; fileNo < 12; ++fileNo) {
						std::stringstream t00; t00 << std::setw(4) << std::setfill('0') << hx + fileNo - 2 << std::setw(4) << std::setfill('0') << hy << ".crn";
						f00 = osgDB::findDataFile(t00.str(), options);
						if (!f00.empty()) {
							fileExists[fileNo] = true;
							if (fileNo < 11 ) fileExists[fileNo + 1] = false;
							if (fileNo < 10 ) fileExists[fileNo + 2] = false;
							fileNo += 2;
							skip = true;
						} else {
							fileExists[fileNo] = false;
							if (skip) {
								skip = false;
								fileNo -= 3;
							}
						}
					}
					if (fileExists[2]) {
						fileExists[0] = fileExists[1] = false;
					} else {
						if (fileExists[1]) fileExists[0] = false;
					}

					int gapStart = 2;//left edge of area to fill
					for (int fileNo = 0; fileNo < 12; fileNo += 1) {
						int leftCrop = fileNo < 2 ? 2 - fileNo : 0;
						int rightCrop = fileNo > 9 ? 12 - fileNo : 3;
						if (fileExists[fileNo]) {
							if (gapStart < fileNo) {
								subTiles.emplace_back(0.1f * (gapStart - 2), 0.1f * (fileNo - 2), rowMinY, rowMaxY, std::string());//fill in the hole
							}
							gapStart = fileNo + 3;
							std::stringstream t00; 
							t00 << std::setw(4) << std::setfill('0') << hx + fileNo - 2 << std::setw(4) << std::setfill('0') << hy;
							if ((topCrop != 0) || (bottomCrop != 3) || (leftCrop != 0) || (rightCrop != 3)) t00 << "C" << leftCrop << rightCrop << "3" << topCrop << bottomCrop << "3";
							t00 << ".crn";
							subTiles.emplace_back((fileNo < 2) ? 0.0f : 0.1f * (fileNo - 2), (fileNo > 9) ? 1.0f : 0.1f * (fileNo + 1), rowMinY, rowMaxY, t00.str());
						}
					}
					if (gapStart < 10)  subTiles.emplace_back(0.1f * (gapStart - 2), 1.0f, rowMinY, rowMaxY, std::string());//fill remainder of row
				}
			}
			x = 1000 * x;
			y = 1000 * y;
			if (l == 4) {
				osg::Matrix mat;
				mat.makeTranslate(x + 0.5 * tileSize, y - 0.5 * tileSize, 0.0);
				matTrans = new osg::MatrixTransform(mat);
			}
			if (NULL == img) {
				bool subtiles = false;
				for (std::vector<subTile>::iterator it = subTiles.begin(); it != subTiles.end(); ++it) {
					if (!it->_fileName.empty()) subtiles = true;
				}
				if (!subtiles) return img_rr;
				osg::ProxyNode *prox = new osg::ProxyNode();
				float halfSize = 0.5f * tileSize;
				if (l <= 4) {
					int xOff = (x / 16000) * 16000 + 8000;
					int yOff = ((y + 3999) / 16000) * 16000 + 4000;
					prox->setCenter(osg::Vec3(halfSize + x - xOff, y - halfSize - yOff, 0.0f));
				} else {
					prox->setCenter(osg::Vec3(halfSize + x, y - halfSize, 0.0f));
				}
				prox->setRadius(0.707125f * tileSize);

				int child = 0;
				for (std::vector<subTile>::iterator it = subTiles.begin(); it != subTiles.end(); ++it) {
					if (!it->_fileName.empty()) prox->setFileName(child++, it->_fileName);
				}
				if (matTrans) {
					matTrans->addChild(prox);
					return matTrans;
				}
				return prox;
			}
		} else {
			x = 100 * atoi(basename.substr(0, 4).c_str());
			y = 100 * atoi(basename.substr(4, 4).c_str());
			tileSize = 300;
		}
		if (NULL == img) return img_rr;
		//we have an image
		osg::StateSet *ss = new osg::StateSet();
		osg::Texture2D *tex = new osg::Texture2D(img);
		tex->setResizeNonPowerOfTwoHint(false);
		tex->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR_MIPMAP_LINEAR);
		tex->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
		tex->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
		tex->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
		ss->setTextureAttributeAndModes(0, tex, osg::StateAttribute::ON);
		float uOff = -0.125f * 1000.0f / (float)(tileSize);	// left top -1999.875
		float vOff =  0.125f * 1000.0f / (float)(tileSize);	// left top 629999.875
		if (tileSize == 300) {
			uOff = -0.0375f * 1000.0f / (float)(tileSize); //Harlingen left top: 154999.950000 Waadhoeke left top: 156999.975000
			vOff = -0.025f * 1000.0f / (float)(tileSize);  //Harlingen left top: 581000.025000 Waadhoeke left top: 596000.025000
		}
		float uMin = (12.0f   + uOff) / 1024.0f;
		float uMax = (1012.0f + uOff) / 1024.0f;
		float vMin = (12.0f   + vOff) / 1024.0f;
		float vMax = (1012.0f + vOff) / 1024.0f;

		float xMin = (float)((x     ));
		float xMax = (float)((x + tileSize ));
		float yMin = (float)((y - tileSize ));
		float yMax = (float)((y     ));
		bool subtiles = false;
		for (std::vector<subTile>::iterator it = subTiles.begin(); it != subTiles.end(); ++it) {
			if (!it->_fileName.empty()) subtiles = true;
		}
		if (cropTile) {
			int cxMin = atoi(basename.substr(9, 1).c_str());
			int cxMax = atoi(basename.substr(10, 1).c_str());
			int cxDiv = atoi(basename.substr(11, 1).c_str());
			int cyMin = atoi(basename.substr(12, 1).c_str());
			int cyMax = atoi(basename.substr(13, 1).c_str());
			int cyDiv = atoi(basename.substr(14, 1).c_str());
			if (cxDiv > 0) {
				if (cxMin > 0) {
					xMin = (float)(x)+((float)(cxMin * tileSize) / (float)(cxDiv));
					uMin = (12.0f + uOff + ((float)(cxMin * 1000) / (float)(cxDiv))) / 1024.0f;
				}
				if (cxMax > 0) {
					xMax = (float)(x)+((float)(cxMax * tileSize) / (float)(cxDiv));
					uMax = (12.0f + uOff + ((float)(cxMax * 1000) / (float)(cxDiv))) / 1024.0f;
				}
			}
			if (cyDiv > 0) {
				if (cyMax > 0) {
					yMin = (float)(y) -((float)(cyMax * tileSize) / (float)(cyDiv));
					vMax = (12.0f + vOff + ((float)(cyMax * 1000) / (float)(cyDiv))) / 1024.0f;
				}
				if (cyMin > 0) {
					yMax = (float)(y)-((float)(cyMin * tileSize) / (float)(cyDiv));
					vMin = (12.0f + vOff + ((float)(cyMin * 1000) / (float)(cyDiv))) / 1024.0f;
				}
			}
		}
		if ((l < 4) || ((l == 4) && subtiles)) {
			int xOff = ((int)((xMin+xMax)/2.0f) / 16000) * 16000 + 8000;
			int yOff = (((int)((yMin + yMax) / 2.0f) + 3999) / 16000) * 16000 + 4000;
			xMin -= xOff;
			xMax -= xOff;
			yMin -= yOff;
			yMax -= yOff;
		}
		osg::Vec3Array* norms = new osg::Vec3Array(osg::Array::Binding::BIND_OVERALL, 1);
		(*norms)[0].set(0.0f,0.0f,1.0f);
		osg::DrawElementsUShort *de = new osg::DrawElementsUShort(GL_TRIANGLES, 6);//0 1 2 1 3 2 
		(*de)[0] = 0; (*de)[1] = 1; (*de)[2] = 2; (*de)[3] = 1; (*de)[4] = 3; (*de)[5] = 2;

		if (subtiles) {
			//CREATE group WITH SUBTILES in plod
			osg::Group *grp = matTrans ? matTrans->asGroup() : new osg::Group();
			grp->setStateSet(ss);
			for (std::vector<subTile>::iterator it = subTiles.begin(); it != subTiles.end(); ++it) {
				float txMin = xMin + ((xMax -xMin) * it->_xMin);
				float txMax = xMin + ((xMax -xMin) * it->_xMax);
				float tyMin = yMin + ((yMax -yMin) * it->_yMin);
				float tyMax = yMin + ((yMax -yMin) * it->_yMax);
				float tuMin = uMin + ((uMax -uMin) * it->_xMin);
				float tuMax = uMin + ((uMax -uMin) * it->_xMax);
				float tvMin = vMin + ((vMax -vMin) * (1.0f -it->_yMax));
				float tvMax = vMin + ((vMax -vMin) * (1.0f -it->_yMin));
				osg::Geode *geo = new osg::Geode;
				osg::Geometry *geom = new osg::Geometry;
				osg::Vec3Array* v = new osg::Vec3Array(osg::Array::Binding::BIND_PER_VERTEX, 4);
				(*v)[0].set(txMin, tyMax, 0.0f);
				(*v)[1].set(txMin, tyMin, 0.0f);
				(*v)[2].set(txMax, tyMax, 0.0f);
				(*v)[3].set(txMax, tyMin, 0.0f);
				geom->setVertexArray(v);
				geom->setNormalArray(norms);
				osg::Vec2Array* t = new osg::Vec2Array(osg::Array::Binding::BIND_PER_VERTEX, 4);
				(*t)[0].set(tuMin, tvMin);
				(*t)[1].set(tuMin, tvMax);
				(*t)[2].set(tuMax, tvMin);
				(*t)[3].set(tuMax, tvMax);
				geom->setTexCoordArray(0, t);
				geom->addPrimitiveSet(de);
				geo->addChild(geom);

				if (it->_fileName.empty()) {
					grp->addChild(geo);
				} else {
					osg::PagedLOD *lod = new osg::PagedLOD();
					lod->setRangeMode(osg::LOD::PIXEL_SIZE_ON_SCREEN);
					lod->setCenter(osg::Vec3(0.5f *(txMin + txMax), 0.5f * (tyMin + tyMax), 0.0f));
					lod->setRadius(0.5f * sqrtf(((txMax - txMin) * (txMax - txMin)) + ((tyMax - tyMin) * (tyMax - tyMin))));
					//lod->setRadius(0.25f * sqrtf(tileSize));don't - tile culls to early
					//float rangeMax = img->s() * 0.707f;// *1.4142f;
					float rangeMax = img->s() * sqrtf(((tuMax - tuMin) * (tuMax - tuMin)) + ((tvMax - tvMin) * (tvMax - tvMin)));
					lod->addChild(geo, 0, rangeMax);
					lod->setFileName(1, it->_fileName);
					lod->setRange(1, rangeMax, FLT_MAX);
					grp->addChild(lod);
				}
			}
			return grp;
		}

		//create a tile
		osg::Geode *geo = new osg::Geode;
		osg::Geometry *geom = new osg::Geometry;
		osg::Vec3Array* v = new osg::Vec3Array(osg::Array::Binding::BIND_PER_VERTEX, 4);
		(*v)[0].set(xMin,yMax,0.0f);
		(*v)[1].set(xMin,yMin,0.0f);
		(*v)[2].set(xMax,yMax,0.0f);
		(*v)[3].set(xMax,yMin,0.0f);
		geom->setVertexArray(v);
		geom->setNormalArray(norms);
		osg::Vec2Array* t = new osg::Vec2Array(osg::Array::Binding::BIND_PER_VERTEX, 4);
		(*t)[0].set(uMin, vMin);
		(*t)[1].set(uMin, vMax);
		(*t)[2].set(uMax, vMin);
		(*t)[3].set(uMax, vMax);
		geom->setTexCoordArray(0, t);
		geom->addPrimitiveSet(de);
		geo->addChild(geom);


		geo->setStateSet(ss);
		return geo;
	}
};

// now register with Registry to instantiate the above
// reader/writer.
REGISTER_OSGPLUGIN(crn, ReaderWriterCRN)
