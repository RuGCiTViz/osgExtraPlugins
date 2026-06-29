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

/* file:        src/osgPlugins/---/ReaderWriterARG.cpp
 * author:      Laurens Voerman
 * copyright:   (C) 2020 Laurens Voerman
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

#define EXTENSION_NAME "arg"


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
#if 0
        OSG_WARN << "Missing parameters for " EXTENSION_NAME " pseudo-loader" << std::endl;
        return false;
#else
        filename = input.substr(0, pos);
        return true;
#endif
    }
#if 1
	// clear the params sting of outher brackets.
	char c = params[params.size()-1];
	if (c == ']' || c == ')') {
		params = params.substr(1, params.size() - 2);
	}
#else
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
#endif
    // strip the "params extension", which must leave a sub-filename.
    filename = input.substr(0, pos );

    return true;
}

///////////////////////////////////////////////////////////////////////////

/**
 * An OSG reader plugin for the ".arg" pseudo-loader, which reads a script
 * file while passing a set of "command line arguments" to the script.
 *
 * Usage: <scriptfile.ext>.[arg1 arg0 ...].arg
 * where:
 *      <scriptfile.ext> = an script filename.
 *      [arg1 arg0 ...] = arguments (brackets required if any argument contains a "." ) 
 *
 * example: osgviewer test.lua.[arg1 arg2 arg3].arg
 */

class ReaderWriterARG : public osgDB::ReaderWriter
{
public:
	ReaderWriterARG()
    {
        supportsExtension(EXTENSION_NAME,"arguments Pseudo loader.");
    }

    virtual const char* className() const { return "arguments pseudo-loader"; }
#if 1
    bool parse(const std::string& fileName, std::string& subFileName, osgDB::ReaderWriter::Options* local_opt) const {
        std::string ext = osgDB::getLowerCaseFileExtension(fileName);
        if (!acceptsExtension(ext))
            return false;

        OSG_INFO << "ReaderWriterARG( \"" << fileName << "\" )" << std::endl;

        // strip the pseudo-loader extension
        std::string tmpName = osgDB::getNameLessExtension(fileName);

        if (tmpName.empty()) return false;

        std::string params;
        if (!getFilenameAndParams(tmpName, subFileName, params))
        {
            return false;
        }

        if (subFileName.empty())
        {
            OSG_WARN << "Missing subfilename for " EXTENSION_NAME " pseudo-loader" << std::endl;
            return false;
        }
        if (params.empty())
        {
            // OSG_WARN << "Missing params for " EXTENSION_NAME " pseudo-loader" << std::endl;
            return true;
        }
        local_opt->setPluginStringData("arguments", params);
        local_opt->setObjectCacheHint(osgDB::ReaderWriter::Options::CACHE_NONE);//don't add this to the cache!
        return true;
    }
    virtual ReadResult readImage(const std::string& fileName, const osgDB::ReaderWriter::Options* options) const
    {
        std::string subFileName;
        osg::ref_ptr<osgDB::ReaderWriter::Options> local_opt = options ? new osgDB::ReaderWriter::Options(*options) : new osgDB::ReaderWriter::Options();
        if (!parse(fileName, subFileName, local_opt.get())) return ReadResult::FILE_NOT_HANDLED;
        return osgDB::readRefImageFile(subFileName, local_opt.get());
    }
    virtual ReadResult readNode(const std::string& fileName, const osgDB::ReaderWriter::Options* options) const {
        std::string subFileName;
        osg::ref_ptr<osgDB::ReaderWriter::Options> local_opt = options ? new osgDB::ReaderWriter::Options(*options) : new osgDB::ReaderWriter::Options();
        if (!parse(fileName, subFileName, local_opt.get())) return ReadResult::FILE_NOT_HANDLED;
        return osgDB::readRefNodeFile(subFileName, local_opt.get());
    }
    virtual ReadResult readObject(const std::string& fileName, const osgDB::ReaderWriter::Options* options) const {
        std::string subFileName;
        osg::ref_ptr<osgDB::ReaderWriter::Options> local_opt = options ? new osgDB::ReaderWriter::Options(*options) : new osgDB::ReaderWriter::Options();
        if (!parse(fileName, subFileName, local_opt.get())) return ReadResult::FILE_NOT_HANDLED;
        return osgDB::readRefObjectFile(subFileName, local_opt.get());
    }

#else
    virtual ReadResult readImage(const std::string& fileName, const osgDB::ReaderWriter::Options* options) const
    {
        std::string ext = osgDB::getLowerCaseFileExtension(fileName);
        if( !acceptsExtension(ext) )
            return ReadResult::FILE_NOT_HANDLED;

        OSG_INFO << "ReaderWriterARG( \"" << fileName << "\" )" << std::endl;

        // strip the pseudo-loader extension
        std::string tmpName = osgDB::getNameLessExtension( fileName );

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
		if (params.empty())
		{
			OSG_WARN << "Missing params for " EXTENSION_NAME " pseudo-loader" << std::endl;
			return ReadResult::FILE_NOT_HANDLED;
		}

		osg::ref_ptr<osgDB::ReaderWriter::Options> local_opt = options ? new osgDB::ReaderWriter::Options(*options) : new osgDB::ReaderWriter::Options();
		local_opt->setPluginStringData("arguments", params);
		local_opt->setObjectCacheHint(osgDB::ReaderWriter::Options::CACHE_NONE);//don't add this to the cache!
		// recursively load the subfile.
		return osgDB::readRefImageFile( subFileName, local_opt.get() );
    }
    	virtual ReadResult readNode(const std::string& fileName, const osgDB::ReaderWriter::Options* options) const {
		std::string ext = osgDB::getLowerCaseFileExtension(fileName);
		if (!acceptsExtension(ext))
			return ReadResult::FILE_NOT_HANDLED;

		OSG_INFO << "ReaderWriterARG( \"" << fileName << "\" )" << std::endl;

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
		if (params.empty())
		{
			OSG_WARN << "Missing params for " EXTENSION_NAME " pseudo-loader" << std::endl;
			return ReadResult::FILE_NOT_HANDLED;
		}

		osg::ref_ptr<osgDB::ReaderWriter::Options> local_opt = options ? new osgDB::ReaderWriter::Options(*options) : new osgDB::ReaderWriter::Options();
		local_opt->setPluginStringData("arguments", params);
		local_opt->setObjectCacheHint(osgDB::ReaderWriter::Options::CACHE_NONE);//don't add this to the cache!
		// recursively load the subfile.
		return osgDB::readRefNodeFile(subFileName, local_opt.get());
	}
	virtual ReadResult readObject(const std::string& fileName, const osgDB::ReaderWriter::Options* options) const {
		return readNode(fileName, options);
	}
#endif

};


// Add ourself to the Registry to instantiate the reader/writer.
REGISTER_OSGPLUGIN(arg, ReaderWriterARG)

/*EOF*/

