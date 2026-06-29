/* -*-c++-*- OpenSceneGraph - Copyright (C) 1998-2004 Robert Osfield 
 *
 * This application is open source and may be redistributed and/or modified   
 * freely and without restriction, both in commericial and non commericial
 * applications, as long as this copyright notice is maintained.
 * 
 * This application is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
*/

/* file:        src/osgPlugins/---/ReaderWriterMIP.cpp
 * author:      Laurens Voerman
 * copyright:   (C) 2009 Laurens Voerman
 * license:     OpenSceneGraph Public License (OSGPL)
*/

#include <osg/Notify>
#include <osg/Matrix>
#include <osg/MatrixTransform>

#include <osgDB/ReaderWriter>
#include <osgDB/FileNameUtils>
#include <osgDB/Registry>
#include <osgDB/ReadFile>
#include <osg/Version>

#include <stdio.h>
#include <sstream>

#define EXTENSION_NAME "mip"


class restoreCacheOptReadFileCallback : public osgDB::Registry::ReadFileCallback
{
	osgDB::ReadFileCallback* _org_rfcb;
	osgDB::Options::CacheHintOptions _cachehint;
	int _pass;
public:
	restoreCacheOptReadFileCallback(osgDB::ReadFileCallback* org_rfcb, osgDB::Options::CacheHintOptions cachehint) :
		_org_rfcb(org_rfcb),
		_cachehint(cachehint),
		_pass(0) {}

	virtual osgDB::ReaderWriter::ReadResult readNode(const std::string& filename, const osgDB::ReaderWriter::Options* options)
	{
		osg::ref_ptr<osgDB::ReadFileCallback> keep_alive(this);
		osgDB::Registry *reg = osgDB::Registry::instance();
		if (_pass > 0) {
			osg::ref_ptr<osgDB::ReaderWriter::Options> modifiedOptions = options ? new osgDB::ReaderWriter::Options(*options) : new osgDB::ReaderWriter::Options();
			modifiedOptions->setObjectCacheHint(_cachehint);//restore caching
			modifiedOptions->setReadFileCallback(_org_rfcb);//I'm out
			return osgDB::readRefNodeFile(filename, modifiedOptions.get());
		}
		++_pass;
		//from Registry ReadNode - need to skip options->readfilecallback (==this) to avoid recursive loop
		if (_org_rfcb) return _org_rfcb->readNode(filename, options);
		osgDB::Registry::ReadFileCallback *_readFileCallback = reg->getReadFileCallback();
		osgDB::ReaderWriter::ReadResult result;
		if (_readFileCallback) result = _readFileCallback->readNode(filename, options);
		else result = reg->readNodeImplementation(filename, options);
		return result;
	}
};
static bool getFilenameAndParams(const std::string& input, std::string& filename, std::string& params)
{
    // find the start of the params list, accounting for nesting of [] and () brackets,
    // note, we are working backwards.
    int noNestedBrackets = 0;
    std::string::size_type pos = input.size();
    for(; pos>0; )
    {
        --pos;
        char c = input[pos];
        if (c==']') ++noNestedBrackets;
        else if (c=='[') --noNestedBrackets;
        else if (c==')') ++noNestedBrackets;
        else if (c=='(') --noNestedBrackets;
        else if (c=='.' && noNestedBrackets==0) break;
    }

    // get the next "extension", which actually contains the pseudo-loader parameters
    params = input.substr(pos+1, std::string::npos );
    if( params.empty() )
    {
        OSG_WARN << "Missing parameters for " EXTENSION_NAME " pseudo-loader" << std::endl;
        return false;
    }

    // clear the params sting of any brackets.
    std::string::size_type params_pos = params.size();
    for(; params_pos>0; )
    {
        --params_pos;
        char c = params[params_pos];
        if (c==']' || c=='[' || c==')' || c=='(')
        {
            params.erase(params_pos,1);
        }
    }

    // strip the "params extension", which must leave a sub-filename.
    filename = input.substr(0, pos );

    return true;
}

///////////////////////////////////////////////////////////////////////////

/**
 * An OSG reader plugin for the ".mip" pseudo-loader, which reads a dds
 * file while skipping the high detail mipmaps.
 * This pseudo-loader make it simple to conserve diskspace when a pagelod
 * uses different mip versions of the same image.
 *
 * Usage: <modelfile.ext>.<x>.mip
 * where:
 *      <modelfile.ext> = an image filename.
 *      <x> = number of mipmaps to skip.
 *
 * example: osgviewer testimage.dds.2.mip 
 */

class ReaderWriterMIP : public osgDB::ReaderWriter
{
public:
    ReaderWriterMIP()
    {
        supportsExtension(EXTENSION_NAME,"mipmap Pseudo loader.");
    }

    virtual const char* className() const { return "mipmap pseudo-loader"; }

    virtual ReadResult readImage(const std::string& fileName, const osgDB::ReaderWriter::Options* options) const
    {
        std::string ext = osgDB::getLowerCaseFileExtension(fileName);
        if( !acceptsExtension(ext) )
            return ReadResult::FILE_NOT_HANDLED;

        OSG_INFO << "ReaderWriterMIP( \"" << fileName << "\" )" << std::endl;

        // strip the pseudo-loader extension
        std::string tmpName = osgDB::getNameLessExtension( fileName );

        if (tmpName.empty())
            return ReadResult::FILE_NOT_HANDLED;

        std::string subFileName, params;
        if (!getFilenameAndParams(tmpName, subFileName, params))
        {
            return ReadResult::FILE_NOT_HANDLED;
        }

        if( subFileName.empty())
        {
            OSG_WARN << "Missing subfilename for " EXTENSION_NAME " pseudo-loader" << std::endl;
            return ReadResult::FILE_NOT_HANDLED;
        }

        OSG_INFO << " params = \"" << params << "\"" << std::endl;
        OSG_INFO << " subFileName = \"" << subFileName << "\"" << std::endl;

		int numMipmapsToSkip = 0;
        int count = sscanf( params.c_str(), "%d", &numMipmapsToSkip );
        if( count != 1 || numMipmapsToSkip < 0)
        {
            OSG_WARN << "Bad parameters for " EXTENSION_NAME " pseudo-loader: \"" << params << "\"" << std::endl;
            return ReadResult::FILE_NOT_HANDLED;
        }

		if (numMipmapsToSkip > 0) {
#if 0
//code to get the sublevel higher res version in memory 
//code works but images are not in cache when loaded from a pagedLOD
//TODO find textureObject?			osg::Object *cached_obj;
			if (numMipmapsToSkip == 1) {
				cached_obj = osgDB::Registry::instance()->getFromObjectCache(subFileName);
			} else {
				std::ostringstream nextMip;
				nextMip << subFileName << "." << numMipmapsToSkip -1 << ".mip";
				cached_obj = osgDB::Registry::instance()->getFromObjectCache(nextMip.str());
			}
			osg::ref_ptr<osg::Image> cached_img = dynamic_cast<osg::Image *>(cached_obj);
			if (cached_img.valid() && (cached_img->getNumMipmapLevels() > 1)) {
				//return cached_obj; // drop a mipmap or mem will not be free!
				int sx = cached_img->s();
				int tx = cached_img->t();
				int rx = cached_img->r();
				osg::ref_ptr<osg::Image> halfImage = new osg::Image;
					if (sx > 1) sx >>= 1;
					if (tx > 1) tx >>= 1;
					if (rx > 1) rx >>= 1;
					halfImage->setImage(sx,tx,1,cached_img->getInternalTextureFormat(),cached_img->getPixelFormat(),
						cached_img->getDataType(), 0, osg::Image::USE_NEW_DELETE, cached_img->getPacking());
					osg::Image::MipmapDataType mmVec;
					for (unsigned int mm=1;mm<=cached_img->getNumMipmapLevels()-2;++mm) {
						mmVec.push_back(cached_img->getMipmapOffset(mm+1) - cached_img->getMipmapOffset(1));
					}
					if (mmVec.size()>0) halfImage->setMipmapLevels(mmVec);//for size calculation
					unsigned int size = halfImage->getTotalSizeInBytesIncludingMipmaps();
					unsigned char* imageData = new unsigned char [size];
					memcpy(imageData, cached_img->getMipmapData(1), size);
					//get the data
					halfImage->setImage(sx,tx,1,cached_img->getInternalTextureFormat(),cached_img->getPixelFormat(),
										cached_img->getDataType(), imageData, osg::Image::USE_NEW_DELETE);
				    if (mmVec.size()>0) halfImage->setMipmapLevels(mmVec);//again, has been cleared when setting the data
         
					// Return Image.
					return halfImage.release();
			}
#endif
            osg::ref_ptr<osgDB::ReaderWriter::Options> modifiedOptions = options ? new osgDB::ReaderWriter::Options(*options) : new osgDB::ReaderWriter::Options();
			std::string optString = std::string(modifiedOptions->getOptionString() + "mmToSkip=" + params);
			modifiedOptions->setOptionString(optString);
			modifiedOptions->setObjectCacheHint(osgDB::ReaderWriter::Options::CACHE_NONE);//don't add this to the cache!
			// recursively load the subfile.
			return osgDB::readRefImageFile( subFileName, modifiedOptions.get() );
		}
        // recursively load the subfile.
        return osgDB::readRefImageFile( subFileName, options );
    }
	virtual ReadResult readNode(const std::string& fileName, const osgDB::ReaderWriter::Options* options) const {
		if (!acceptsExtension(osgDB::getLowerCaseFileExtension(fileName))) return ReadResult::FILE_NOT_HANDLED;
		std::string tmpName = osgDB::getNameLessExtension(fileName);
		if (tmpName.empty()) return ReadResult::FILE_NOT_HANDLED;

		std::string subFileName, params;
		if (!getFilenameAndParams(tmpName, subFileName, params)) return ReadResult::FILE_NOT_HANDLED;
		if (subFileName.empty())
		{
			OSG_WARN << "Missing subfilename for " EXTENSION_NAME " pseudo-loader" << std::endl;
			return ReadResult::FILE_NOT_HANDLED;
		}

		OSG_INFO << " params = \"" << params << "\"" << std::endl;
		OSG_INFO << " subFileName = \"" << subFileName << "\"" << std::endl;

		int numMipmapsToSkip = 0;
		int count = sscanf(params.c_str(), "%d", &numMipmapsToSkip);
		bool recursive = (numMipmapsToSkip < 0);//default to non recursive: only images directly referenced in subfile affected (subfile & images not cached)
		if (recursive) numMipmapsToSkip = -numMipmapsToSkip;

		if (count != 1 )
		{
			OSG_WARN << "Bad parameters for " EXTENSION_NAME " pseudo-loader: \"" << params << "\"" << std::endl;
			return ReadResult::FILE_NOT_HANDLED;
		}

		if (numMipmapsToSkip > 0) {
			osg::ref_ptr<osgDB::ReaderWriter::Options> modifiedOptions = options ? new osgDB::ReaderWriter::Options(*options) : new osgDB::ReaderWriter::Options();
			std::string optString = std::string(modifiedOptions->getOptionString() + "mmToSkip=" + params);
			modifiedOptions->setOptionString(optString);
			if (recursive) {
				modifiedOptions->setObjectCacheHint(osgDB::ReaderWriter::Options::CACHE_NONE);//don't add this to the cache!
				// recursively load the subfile.
				osg::ref_ptr<osg::Node> result = osgDB::readRefNodeFile(subFileName, modifiedOptions.get());
				if (result == NULL) return ReadResult::ERROR_IN_READING_FILE;
				return result;
			}
			//osgDB::Registry *reg = osgDB::Registry::instance();
			osg::ref_ptr<osgDB::ReadFileCallback> org_rfcb = modifiedOptions->getReadFileCallback();// reg->getReadFileCallback();
			osgDB::Options::CacheHintOptions cachehint = options->getObjectCacheHint();
			modifiedOptions->setReadFileCallback(new restoreCacheOptReadFileCallback(org_rfcb.get(), cachehint));
			modifiedOptions->setObjectCacheHint(osgDB::ReaderWriter::Options::CACHE_NONE);//don't add this to the cache!
			// recursively load the subfile.
			osg::ref_ptr<osg::Node> result = osgDB::readRefNodeFile(subFileName, modifiedOptions.get());
			//reg->setReadFileCallback(org_rfcb);
			if (result == NULL) return ReadResult::ERROR_IN_READING_FILE;
			return result;
		}
		// recursively load the subfile.
		osg::ref_ptr<osg::Node> result = osgDB::readRefNodeFile(subFileName, options);
		if (result == NULL) return ReadResult::ERROR_IN_READING_FILE;
		return result;
	}
};


// Add ourself to the Registry to instantiate the reader/writer.
REGISTER_OSGPLUGIN(mip, ReaderWriterMIP)

/*EOF*/

