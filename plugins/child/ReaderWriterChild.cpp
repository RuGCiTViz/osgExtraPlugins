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

/* file:        src/osgPlugins/---/ReaderWriterChild.cpp
 * author:      Laurens Voerman
 * copyright:   (C) 2019 Laurens Voerman
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

#define EXTENSION_NAME "child"


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
 * An OSG reader plugin for the ".child" pseudo-loader, which reads a model
 * file, while skipping all but the selected child in the cull phase.
 * This pseudo-loader make it simple to share arrays and statesets in the
 * lower lod levels of multiple objects.
 *
 * As each child refers to shared objects from the main file, the point is to 
 * keep the main file in the filecache - at least until all requested children 
 * are loaded - but generaly until the last child is unloaded.
 *
 * Usage: <modelfile.ext>.<x>.child
 * where:
 *      <modelfile.ext> = an model group filename.
 *      <x> = number of child to show.
 *
 * example: osgviewer test.osg.2.child 
 */

class ReaderWriterChild : public osgDB::ReaderWriter
{
public:
    ReaderWriterChild()
    {
        supportsExtension(EXTENSION_NAME,"child Pseudo loader.");
    }

    virtual const char* className() const { return "child pseudo-loader"; }

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

		unsigned int childToShow = 0;
		{
			int childToShowTemp = 0;
			int count = sscanf(params.c_str(), "%d", &childToShowTemp);
			if ((count != 1) || (childToShowTemp < 0))
			{
				OSG_WARN << "Bad parameters for " EXTENSION_NAME " pseudo-loader: \"" << params << "\"" << std::endl;
				return ReadResult::FILE_NOT_HANDLED;
			}
			childToShow = childToShowTemp;
		}
		// recursively load the subfile.
		osg::ref_ptr<osg::Node> result;
		if (!options) options = osgDB::Registry::instance()->getOptions();
		bool cacheNodes = ((options != NULL) && ((options->getObjectCacheHint() & osgDB::ReaderWriter::Options::CACHE_NODES) == osgDB::ReaderWriter::Options::CACHE_NODES));
		result = osgDB::readRefNodeFile(subFileName, options);
		if (result == NULL) {
			OSG_WARN << EXTENSION_NAME " pseudo-loader: failed to load \"" << fileName << "\"; subfile \"" << subFileName << "\" failed to load." << std::endl;
			return ReadResult::ERROR_IN_READING_FILE;
		}
		osg::ref_ptr<osg::Group> fileMainGroup = result->asGroup();
		if (fileMainGroup == NULL) {
			OSG_WARN << EXTENSION_NAME " pseudo-loader: failed to load \"" << fileName << "\"; subfile \"" << subFileName << "\" top node is not a Group." << std::endl;
			return ReadResult::ERROR_IN_READING_FILE;
		}
		if (fileMainGroup->getNumChildren() <= childToShow) {
			OSG_WARN << EXTENSION_NAME " pseudo-loader: failed to load \"" << fileName << "\"; subfile \"" << subFileName << "\" top Group node doesn't have that child node." << std::endl;
			return ReadResult::ERROR_IN_READING_FILE;
		}
		osg::ref_ptr<osg::Node> subGraph = fileMainGroup->getChild(childToShow);
		std::string::size_type nextComma = params.find_first_of(',', 0);
		//reparse params if we want a deeper decentant
		while (nextComma != std::string::npos) {
			std::istringstream(params.substr(nextComma + 1)) >> childToShow;
			osg::ref_ptr<osg::Group> subGroup = subGraph->asGroup();
			if (!subGroup.valid()) {
				OSG_WARN << EXTENSION_NAME " pseudo-loader: failed to load \"" << fileName << "\"; subfile \"" << subFileName << "\" path " << params.substr(0, nextComma + 1) << " is not a group; it's a " << subGraph->className() << std::endl;
				return ReadResult::ERROR_IN_READING_FILE;
			}
			if ((childToShow < 0) || (childToShow >= subGroup->getNumChildren())) {
				OSG_WARN << EXTENSION_NAME " pseudo-loader: failed to load \"" << fileName << "\"; subfile \"" << subFileName << "\" path " << params.substr(0, nextComma + 1) 
					<< " " << subGraph->className() << "only has "<< subGroup->getNumChildren() << " children; not a child " << childToShow << std::endl;
				return ReadResult::ERROR_IN_READING_FILE;
			}
			subGraph = subGroup->getChild(childToShow);
			nextComma = params.find_first_of(',', nextComma + 1);
		}
		if (!cacheNodes) return subGraph;

		osg::ref_ptr<osg::Group> myGroup = new osg::Group;
		myGroup->addChild(subGraph);
		osg::ref_ptr<osg::Group> fileHoldGroup = new osg::Group;
		fileHoldGroup->setNodeMask(0);//Never show; allways cull
		fileHoldGroup->addChild(result);
		myGroup->addChild(fileHoldGroup);//but hold on to a reference so it will stay in memory/cache
		return myGroup;
	}
	virtual ReadResult readObject(const std::string& fileName, const osgDB::ReaderWriter::Options* options) const {
		return readNode(fileName, options);
	}
};


// Add ourself to the Registry to instantiate the reader/writer.
REGISTER_OSGPLUGIN(EXTENSION_NAME, ReaderWriterChild)

/*EOF*/

