/**********************************************************************
*
*    FILE:            ReaderWriterWtms.cpp
*
*    DESCRIPTION:    Class for reading a tiled quad tree database
*
*    CREATED BY:     Laurens Voerman, l.voerman@rug.nl
*
*    HISTORY:        Created   19.02.2020
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

osg::Image* cropImage(int width, int height, int depth, int s_offset, int t_offset, int r_offset, const osg::Image* source) {
	if (!source) return NULL;
	int s = source->s();
	int t = source->t();
	int r = source->r();
	GLenum pixelFormat = source->getPixelFormat();
	GLenum type = source->getDataType();
	osg::Vec3i footprint = osg::Image::computeBlockFootprint(pixelFormat);
	if ((s_offset % footprint.x()) || (t_offset % footprint.y()) || (r_offset % footprint.z())) {
		OSG_WARN << "Error cropImage() did not succeed : offset " << s_offset << "," << t_offset << "," << r_offset << " not multiple of footprint " << footprint.x() << "," << footprint.y() << "," << footprint.z() << std::endl;
		return NULL;
	}
	osg::Image* target = new osg::Image();
	target->allocateImage(width,  height,  depth, pixelFormat, type);

	unsigned int src_rowWidthInBlocks = (s + footprint.x() - 1) / footprint.x();
	unsigned int src_colHeightInBlocks = (t + footprint.y() - 1) / footprint.y();
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
	for (unsigned int row = 0; row < colCopyInBlocks; row += 1) { //copy blocks in a row, footprint.y() rows at a time
		memcpy(data_dst, data_src, rowCopyInBlocks * blockSizeInBytes);
		data_src += srcRowStepinBytes;
		data_dst += dstRowStepinBytes;
	}
	return target;
}

class ReaderWriterWmts : public osgDB::ReaderWriter
{
	osg::ref_ptr<osg::Vec3Array> norms;
	osg::ref_ptr<osg::DrawElementsUByte> drwelemp;
	osg::ref_ptr<osg::DrawElementsUByte> drwelemt;
	osg::ref_ptr<osg::Vec2Array> tcQuad[5];
public:

	ReaderWriterWmts()
	{
		if (norms == NULL) {
			norms = new osg::Vec3Array(osg::Array::Binding::BIND_OVERALL, 1);
			(*norms)[0].set(0.0f, 0.0f, 1.0f);
		}
		if (drwelemp == NULL) {
			drwelemp = new osg::DrawElementsUByte(GL_PATCHES, 4);//0 1 2 1 3 2 
			(*drwelemp)[0] = 0; (*drwelemp)[1] = 2; (*drwelemp)[2] = 3; (*drwelemp)[3] = 1;
			drwelemt = new osg::DrawElementsUByte(GL_TRIANGLES, 6);//0 1 2 1 3 2 
			(*drwelemt)[0] = 0; (*drwelemt)[1] = 1; (*drwelemt)[2] = 2; (*drwelemt)[3] = 1; (*drwelemt)[4] = 3; (*drwelemt)[5] = 2;
		}
		if (tcQuad[0] == NULL) {
			osg::Vec2Array* t = new osg::Vec2Array(osg::Array::Binding::BIND_PER_VERTEX, 4);
			float uMin = 0.0f; //left 
			float uMax = 0.5f;
			float vMin = 0.0f; //bottom
			float vMax = 0.5f;
			(*t)[0].set(uMin, vMax);
			(*t)[1].set(uMin, vMin);
			(*t)[2].set(uMax, vMax);
			(*t)[3].set(uMax, vMin);
			tcQuad[0] = t;
			t = new osg::Vec2Array(osg::Array::Binding::BIND_PER_VERTEX, 4);
			uMin = 0.5f;//right 
			uMax = 1.0f;
			vMin = 0.0f; //bottom
			vMax = 0.5f;
			(*t)[0].set(uMin, vMax);
			(*t)[1].set(uMin, vMin);
			(*t)[2].set(uMax, vMax);
			(*t)[3].set(uMax, vMin);
			tcQuad[1] = t;
			t = new osg::Vec2Array(osg::Array::Binding::BIND_PER_VERTEX, 4);
			uMin = 0.0f;//left
			uMax = 0.5f;
			vMin = 0.5f; //top
			vMax = 1.0f;
			(*t)[0].set(uMin, vMax);
			(*t)[1].set(uMin, vMin);
			(*t)[2].set(uMax, vMax);
			(*t)[3].set(uMax, vMin);
			tcQuad[2] = t;
			t = new osg::Vec2Array(osg::Array::Binding::BIND_PER_VERTEX, 4);
			uMin = 0.5f;//right 
			uMax = 1.0f;
			vMin = 0.5f; //top?
			vMax = 1.0f;
			(*t)[0].set(uMin, vMax);
			(*t)[1].set(uMin, vMin);
			(*t)[2].set(uMax, vMax);
			(*t)[3].set(uMax, vMin);
			tcQuad[3] = t;
			t = new osg::Vec2Array(osg::Array::Binding::BIND_PER_VERTEX, 4);
			(*t)[0].set(0.0f, 1.0f);
			(*t)[1].set(0.0f, 0.0f);
			(*t)[2].set(1.0f, 1.0f);
			(*t)[3].set(1.0f, 0.0f);
			tcQuad[4] = t;
		}
        supportsExtension("wmts","web map tile server pseudo format");
    }

    virtual const char* className() const
    {
        return "wmts - web map tile server pseudo format reader";
    }

    virtual ReadResult readObject(const std::string& file, const osgDB::ReaderWriter::Options* options) const
    {
        return readNode(file,options);
    }

	osg::Node *createTile(bool heightMap, osg::StateSet *ss, float xMin, float xMax, float yMin, float yMax, float zMin, float zMax, float uMin, float uMax, float vMin, float vMax) const {
		//static osg::Vec3Array* norms = NULL;//crash on data reload; array deleted but pointer not NULL
		//static osg::DrawElementsUByte *de = NULL;
		osg::Geometry *geom = new osg::Geometry;
		osg::Vec3Array* v = new osg::Vec3Array(osg::Array::Binding::BIND_PER_VERTEX, 4);
		(*v)[0].set(xMin, yMax, zMin);
		(*v)[1].set(xMin, yMin, zMax);
		(*v)[2].set(xMax, yMax, zMin);
		(*v)[3].set(xMax, yMin, zMin);
		geom->setVertexArray(v);
		geom->setNormalArray(norms);
		bool quad = true;
		int qindex = 0;

		if (uMin == 0.5f) {
			if (uMax != 1.0f) quad = false;
			qindex = 1;//right
		} else {
			if (uMin != 0.0f) quad = false;
			if (uMax != 0.5f) {
				if ((uMax == 1.0f) && (vMin == 0.0f) && (vMax == 1.0f)) qindex = 4;
				else quad = false;
			}
		}
		if (vMin == 0.5f) {
			if (vMax != 1.0f) quad = false;
			qindex += 2; //top
		}
		else {
			if (vMin != 0.0f) quad = false;
			if ((vMax != 1.0f) && (qindex !=4)) quad = false;
		}

		if (quad) {
			if (heightMap) geom->setVertexAttribArray(3, tcQuad[qindex]);
			else geom->setTexCoordArray(0, tcQuad[qindex]);
		} else {
			osg::Vec2Array* t = new osg::Vec2Array(osg::Array::Binding::BIND_PER_VERTEX, 4);
			(*t)[0].set(uMin, vMax);
			(*t)[1].set(uMin, vMin);
			(*t)[2].set(uMax, vMax);
			(*t)[3].set(uMax, vMin);
			if (heightMap) geom->setVertexAttribArray(3, t);
			else geom->setTexCoordArray(0, t);
		}
		if (heightMap) geom->addPrimitiveSet(drwelemp.get());
		else geom->addPrimitiveSet(drwelemt.get());

		geom->setStateSet(ss);
		return geom;
	}

	osg::Node * createPagedLOD(bool heightMap, osg::StateSet *ss, bool v1, const std::string& urlBase, const std::string& urlTail, int level, int xTile, int yTile, float xMin, float xMax, float yMin, float yMax, float zMin, float zMax, float uMin, float uMax, float vMin, float vMax) const {
		//if (level > 15) return createTile(heightMap, ss, xMin, xMax, yMin, yMax, zMin, zMax, uMin, uMax, vMin, vMax);
		//if (level > 10) return createTile(heightMap, xMin, xMax, yMin, yMax, zMin, zMax, uMin, uMax, vMin, vMax);
		osg::PagedLOD *lod = new osg::PagedLOD();
		lod->setRangeMode(osg::LOD::PIXEL_SIZE_ON_SCREEN);
		lod->setCenter(osg::Vec3(0.5f * (xMin + xMax), 0.5f * (yMin + yMax), 0.5f * (zMin + zMax)));
		lod->setRadius(0.5f * sqrtf(((xMax - xMin) * (xMax - xMin)) + ((yMax - yMin) * (yMax - yMin)) + ((zMax - zMin) * (zMax - zMin))));
		lod->setCenter(osg::Vec3(0.5f * (xMin + xMax), 0.5f * (yMin + yMax), 0.0f ));
		lod->setRadius(0.5f * sqrtf(((xMax - xMin) * (xMax - xMin)) + ((yMax - yMin) * (yMax - yMin)) ));

		float rangeMax = 128.0f * 1.414f;
		osg::Node *geode = createTile(heightMap, ss, xMin, xMax, yMin, yMax, zMin, zMax, uMin, uMax, vMin, vMax);

//#include <osg/ShapeDrawable>
//		osg::Shape *sphere = new osg::Sphere(lod->getCenter(), lod->getRadius());//DEBUG
//		geode->addChild(new osg::ShapeDrawable(sphere));
		lod->addChild(geode, 0, rangeMax);
		std::stringstream tileURL; 
		if (v1) {
			tileURL << urlBase << level << "/" << xTile << "/" << yTile << urlTail;
		} else {
			if (level < 10) {
				tileURL << urlBase << "0" << level << "&TileCol=" << xTile << "&TileRow=" << yTile << urlTail;
			} else {
				tileURL << urlBase <<        level << "&TileCol=" << xTile << "&TileRow=" << yTile << urlTail;
			}
		}
		lod->setFileName(1, tileURL.str());
		lod->setRange(1, rangeMax, FLT_MAX);
		return lod;
	}

	osg::ref_ptr<osg::Image> getImage(const std::string& fileName, const Options *options) const {
		osg::ref_ptr<osg::Image> img;
		bool cacheImages = options ? (options->getObjectCacheHint() & osgDB::Options::CACHE_IMAGES) != 0 : true; //default to caching with no options
		osg::ref_ptr<osgDB::FileCache> fileCache = osgDB::Registry::instance()->getFileCache();
		if (cacheImages && (fileCache.valid() && fileCache->isFileAppropriateForFileCache(fileName))) {
			std::string cacheFileName = fileCache->createCacheFileName(fileName);

			osg::ref_ptr<osg::Object> objectFromCache = osgDB::Registry::instance()->getRefFromObjectCache(fileName);//read fileName from ObjectCache memory
			if (!objectFromCache)
			{
				if (fileCache->existsInCache(fileName))
				{
					objectFromCache = osgDB::Registry::instance()->getRefFromObjectCache(cacheFileName);//read cacheFileName from ObjectCache memory
					if (!objectFromCache) {
						//check size cacheFileName
						struct _stati64 fileStat;
						if (_stati64(cacheFileName.c_str(), &fileStat) == 0) {
							bool emptyCacheFile = fileStat.st_size == 0;
							if (emptyCacheFile) {
								return NULL;
							}
						}
						ReaderWriter::ReadResult rr = fileCache->readImage(fileName, options);//read cacheFileName from disk
						objectFromCache = rr.getObject();
					}
				}
			}
			img = dynamic_cast<osg::Image*>(objectFromCache.get());
			if (!img.valid()) {
				img = osgDB::readRefImageFile(fileName + ".curl",  options);//read remote url
				if (!fileCache->existsInCache(fileName)) {
					if (img.valid()) {
						fileCache->writeImage(*(img), fileName, options);//write cacheFileName
					} else {
						//fileCache->writeImage(*(img), fileName, options);//write empty file to cacheFileName
						if (!cacheFileName.empty()) {
							std::string cacheFilePath = osgDB::getFilePath(cacheFileName);
							if (osgDB::fileExists(cacheFilePath) || osgDB::makeDirectory(cacheFilePath)) {
								FILE* cacheF = osgDB::fopen(cacheFileName.c_str(), "w");
								if (cacheF) fclose(cacheF);
							}
						}
					}
				}
			}
		} else {
			img = osgDB::readRefImageFile(fileName, options);
		}
		return img;
	}
	bool imageMinMax(osg::Image* img, bool heightMap, unsigned int& RGBmax, unsigned int& RGBmin) const {
		if (img) {//check for white images
			bool pureWhite = img->isDataContiguous();
			//pureWhite = false;
			if (pureWhite) pureWhite = (((img->getPixelFormat() == GL_RGBA) || (img->getPixelFormat() == GL_RGB)) && (img->s() > 3));
			if (pureWhite) {
				const unsigned int* data = static_cast<const unsigned int*>(img->getDataPointer());
				if (img->getPixelFormat() == GL_RGB) {
					for (int p = 0; p * sizeof(unsigned int) < img->getTotalSizeInBytes(); ++p) {
						if (data[p] != 0xffffffff) {
							pureWhite = false;
							break;
						}
					}
				}
				else {//GL_RGBA
					unsigned int RGBcolor = 0;
					for (int p = 0; p * sizeof(unsigned int) < img->getTotalSizeInBytes(); ++p) {
						unsigned int RGBApixel = data[p];
						unsigned int RGBpixel = data[p] & 0x00ffffff; //ABGR
						if ((RGBApixel & 0xff000000) != 0) {//not transparent
							if (RGBcolor != 0) {
								if (RGBpixel != RGBcolor) {
									pureWhite = false;//not solid color
									//break;
									if (!heightMap)	break;
									unsigned int RGBheight = RGBpixel;
									osg::swapBytes4((char*)&RGBheight);
									if (RGBheight > RGBmax) RGBmax = RGBheight;//RGB0
									if (RGBheight < RGBmin) RGBmin = RGBheight;
								}
							}
							else {
								RGBcolor = RGBpixel;
							}
						}
					}
				}
			}
			return pureWhite;
		}
		return false;
	}
	virtual ReadResult readGeodataNode(const std::string& fileName, const osgDB::ReaderWriter::Options* options) const {
		//subFileName now stripped from ".wmts"
		// http://geodata.nationaalgeoregister.nl/luchtfoto/rgb/wmts/2019_ortho25/EPSG%3A28992/2/1/1.jpeg.15

		// https://www.geonovum.nl/uploads/documents/nederlandse_richtlijn_tiling_-_versie_1.1.pdf

		unsigned int maxLevel = 15;
		unsigned int level = 0;
		unsigned int xTile = 0;
		unsigned int yTile = 0;
		bool heightMap = false; //generate triangles: no tesselation shader
		//parse 2/1/1.jpeg.14
		std::string::size_type dot = fileName.find_last_of('.');
		if (dot == std::string::npos) return ReadResult::FILE_NOT_HANDLED;
		maxLevel = atoi(fileName.substr(dot + 1).c_str());
		std::string subFileName(fileName.begin(), fileName.begin() + dot);
		dot = subFileName.find_last_of('.');
		if (dot == std::string::npos) return ReadResult::FILE_NOT_HANDLED;
		std::string imageFileExt = subFileName.substr(dot);//.jpeg
		bool luaFile = imageFileExt == ".lua";
		std::string fileExt = fileName.substr(dot) + ".wmts";//.jpeg.15.wmts
		std::string url = subFileName;
		std::string url2; // luaFile ONLY; arial texture
		subFileName.resize(dot);

		std::string::size_type qm = subFileName.find_last_of('?');
		std::string::size_type slash = fileName.substr(0,qm).find_last_of('/');
		if (slash == std::string::npos) return ReadResult::FILE_NOT_HANDLED;
		yTile = atoi(subFileName.substr(slash+1).c_str());
		bool v1 = true;
		if (yTile == 0) {
			if (subFileName.substr(slash + 1, 1) == "v") v1 = false;
		}
		if (v1) {
			
			if (qm != std::string::npos) {
				//for shape https://api.ellipsis-drive.com/v3/path/22f5740b-1276-42e0-9d2d-dd2a9745e928/raster/timestamp/c936131a-2c4f-44ce-b321-a085c6c0ce3b/
				//          tile/13/5048/2870?style=4202a0e1-781d-4ce9-b2d7-ff9a99fe795c&epsg=28992.png.13.wmts
				//retain arguments untouched:
				fileExt = subFileName.substr(qm) + fileExt;
				url = subFileName;//v2 url does not have extention at the end
				heightMap = true; //generate patches for tesselation shader
			}

			subFileName.resize(slash);
			slash = subFileName.find_last_of('/');
			if (slash == std::string::npos) return ReadResult::FILE_NOT_HANDLED;
			xTile = atoi(subFileName.substr(slash + 1).c_str());
			subFileName.resize(slash);
			slash = subFileName.find_last_of('/');
			if (slash == std::string::npos) return ReadResult::FILE_NOT_HANDLED;
			level = atoi(subFileName.substr(slash + 1).c_str());
			subFileName.resize(slash + 1);
		} else {
			url = subFileName;//v2 url does not have extention at the end

			//v2 find x,y, level
			// fileName shape https://service.pdok.nl/brt/achtergrondkaart/wmts/v2_0?layer=standaard&style=default&tilematrixset=EPSG%3A28992&Service=WMTS&Request=GetTile&Version=1.0.0&Format=image%2Fpng&TileMatrix=00&TileCol=0&TileRow=0.png.14
			// subFileName shape https://service.pdok.nl/brt/achtergrondkaart/wmts/v2_0?layer=standaard&style=default&tilematrixset=EPSG%3A28992&Service=WMTS&Request=GetTile&Version=1.0.0&Format=image%2Fpng&TileMatrix=00&TileCol=0&TileRow=0
			std::string::size_type amp = subFileName.find_last_of('&');
			if (amp == std::string::npos) {
				OSG_WARN << "ReadWmtsFile warning: couldn't find apersand while looking for &TileRow=." << std::endl;
				return ReadResult::FILE_NOT_HANDLED;
			}
			if (subFileName.substr(amp + 1, 8) == "TileRow=") {
				yTile = atof(subFileName.substr(amp + 9).c_str());
				subFileName.resize(amp);
			}
			amp = subFileName.find_last_of('&');
			if (amp == std::string::npos) {
				OSG_WARN << "ReadWmtsFile warning: couldn't find apersand while looking for &TileCol=." << std::endl;
				return ReadResult::FILE_NOT_HANDLED;
			}
			if (subFileName.substr(amp + 1, 8) == "TileCol=") {
				xTile = atof(subFileName.substr(amp + 9).c_str());
				subFileName.resize(amp);
			}
			amp = subFileName.find_last_of('&');
			if (amp == std::string::npos) {
				OSG_WARN << "ReadWmtsFile warning: couldn't find apersand while looking for &TileMatix=." << std::endl;
				return ReadResult::FILE_NOT_HANDLED;
			}
			if (subFileName.substr(amp + 1, 11) == "TileMatrix=") {
				level = atof(subFileName.substr(amp + 12).c_str());
				subFileName.resize(amp+12); //keep &TileMatrix=
			}
		}
		if (luaFile) {
			std::stringstream argString;
			argString << level << " " << xTile << " " << yTile;
			std::string luaFileName = subFileName.substr(0, subFileName.length() - 1);
			url = luaFileName + "." + argString.str() + " 0.arg";
			url2 = luaFileName + "." + argString.str() + " 1.arg";
			std::string absFileName = osgDB::findDataFile(luaFileName, options);
			subFileName = absFileName + "/";//use absolute path for pagedLOD's
		}
		unsigned int nextXTile = xTile * 2;
		unsigned int nextYTile = yTile * 2;
		unsigned int nextLevel = level + 1;

		osg::MatrixTransform *mt = NULL;
#if 1
		//matrixTransform on every 4th level for better numeric stability in the z buffer:
		if ((level > 0) && ((level & 3) == 0)) {
			double s, t1, t2;
			s = 1.0 / (1 << 4);//scale 1/16
			t1 = (xTile & 0xF) * s;
			t2 = (yTile & 0xF) * s;
			osg::Matrix mat(s, 0, 0, 0, 0, s, 0, 0, 0, 0, 1, 0, t1, -t2, 0, 1);
			mt = new osg::MatrixTransform(mat);
		}
		level &= 3;
		int mask = (1 << level) - 1; 
		xTile &= mask;
		yTile &= mask;
#endif

		float scaleFactor = 1 << level;
		float xsize = 1.0f / scaleFactor;
		float ysize = 1.0f / scaleFactor;
		float xMin = xsize *  xTile;
		float xMax = xsize * (xTile + 1);
		float yMax = -ysize *  yTile;
		float yMin = -ysize * (yTile + 1);
		float uMin = 0.0f;
		float uMax = 1.0f;
		float vMin = 0.0f;
		float vMax = 1.0f;

		float xMid = (xMin + xMax) * 0.5f;
		float yMid = (yMin + yMax) * 0.5f;
		float uMid = (uMin + uMax) * 0.5f;
		float vMid = (vMin + vMax) * 0.5f;

		osg::ref_ptr<osg::Image> img = getImage(url, options);

		osg::ref_ptr<osg::StateSet> pss = NULL;
		//osg::BoundingSphere bsphere;
		float zMax = 0.0f;
		float zMin = 0.0f;

		if (options) {
			osg::observer_ptr<osg::Group> parent = options->getParentGroup();
			if (parent.valid() && parent->getNumChildren()>0) {
				const osg::StateSet *ps = parent->getChild(0)->getStateSet();
				pss = const_cast<osg::StateSet *>(ps);
#if 0
				osg::Node* geom = parent->getChild(0);
				if (geom) {
					osg::BoundingSphere bound = geom->getBound();
					zMin = bound.center().z() - bound.radius();
					zMax = bound.center().z() + bound.radius();
				}
#endif
			}
		}
		unsigned int RGBmax = 0;
		unsigned int RGBmin = ~0;
		bool pureWhite = imageMinMax(img, heightMap, RGBmax, RGBmin);

		osg::StateSet* ss = new osg::StateSet();
		if (url2.length()) {//second texture url for luafile
			osg::ref_ptr<osg::Image> img2 = getImage(url2, options);
			//todo check for white image...
			if (img2.valid()) {
				if (!img.valid() && pss.valid()) {//no valid height image
					osg::Texture* tex = static_cast<osg::Texture*>(pss->getTextureAttribute(0, osg::StateAttribute::TEXTURE));
					osg::Image* pimg = tex ? tex->getImage(0) : NULL;
					if (!pimg && options) {
						// if (luaFile && heightMap) group->setUserData(img);
						osg::observer_ptr<osg::Group> parent = options->getParentGroup();
						if (parent.valid() && parent->getNumChildren() > 0) {
							osg::UserDataContainer *udc = parent->getParent(0)->getUserDataContainer();
							if (udc) {
								pimg = dynamic_cast<osg::Image*>(udc->getUserData());
							}
						}
					}
					if (pimg) {
						int s = pimg->s();
						int t = pimg->t();
						int r = pimg->r();
						if ((s > 8) && (t > 8)) {
							GLenum pixelFormat = pimg->getPixelFormat();
							GLenum type = pimg->getDataType();
							int s_offset = 0;
							if ((nextXTile & 2) == 2) s_offset = s / 2;
							int t_offset = 0;
							if ((nextYTile & 2) != 2) t_offset = t / 2;//CHECK!
							//img->allocateImage(s / 2, t / 2, r, pixelFormat, type);
							img = cropImage(s / 2, t/2, 1, s_offset, t_offset, 0, pimg);
							bool pureWhite = imageMinMax(img, heightMap, RGBmax, RGBmin);
						}
					}
				}
				osg::Texture2D* Arial = new osg::Texture2D(img2.get());
				Arial->setResizeNonPowerOfTwoHint(false);
				Arial->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);//osg::Texture::LINEAR_MIPMAP_LINEAR
				//Arial->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);default
				Arial->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
				Arial->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
				ss->setTextureAttributeAndModes(1, Arial, osg::StateAttribute::ON);
#define NEARPIX 0
#if NEARPIX
				Arial->setFilter(osg::Texture::MIN_FILTER, osg::Texture::NEAREST);//osg::Texture::LINEAR_MIPMAP_LINEAR
				Arial->setFilter(osg::Texture::MAG_FILTER, osg::Texture::NEAREST);
#endif
			}
		}
		if (heightMap && img.valid() && (RGBmax > RGBmin)) {
			zMax = (0.1f * (RGBmax >> 8)) - 10000.0f;
			zMin = (0.1f * (RGBmin >> 8)) - 10000.0f;
		}

		if (!img.valid()) {//no valid image, use parent image
			if ((nextYTile & 2) == 2) vMax = vMid;
			else vMin = vMid;
			if ((nextXTile & 2) == 2) uMin = uMid;
			else uMax = uMid;

			osg::Node *tile = createTile(heightMap, pss.get(), xMin, xMax, yMin, yMax, zMin, zMax, uMin, uMax, vMin, vMax);
			if (mt) {
				mt->addChild(tile);
				return mt;
			}
			return tile;
		}
		bool toplevel = NULL != pss.get();
		if (heightMap && toplevel) {
			osg::PatchParameter* pp = new osg::PatchParameter(4);
			ss->setAttributeAndModes(pp);
		}
		osg::Texture2D *tex = new osg::Texture2D(img.get());
		tex->setResizeNonPowerOfTwoHint(false);
		tex->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);//osg::Texture::LINEAR_MIPMAP_LINEAR
		//tex->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);default
		tex->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
		tex->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
		ss->setTextureAttributeAndModes(0, tex, osg::StateAttribute::ON);
		if (nextLevel > maxLevel) {
#if 0
			osg::Node *tile = createTile(heightMap, ss, xMin, xMax, yMin, yMax, zMin, zMax, uMin, uMax, vMin, vMax);
#else
			// increase final heightMap level to 2x2 tile
			// should(!) not help - for testing only...
			osg::Group* tile = new osg::Group;
			tile->setStateSet(ss);
			ss = NULL;
			{
				int numRows = 2;
				int numCols = numRows;
				float yWidth = (yMax - yMin) / (float)numRows;
				float xWidth = (xMax - xMin) / (float)numCols;
				float vWidth = (vMax - vMin) / (float)numRows;
				float uWidth = (uMax - uMin) / (float)numCols;
				for (int row = 0; row < numRows; ++row) {
					float yStart = yMin + row * yWidth;
					float yEnd = yStart + yWidth;
					float vStart = vMin + row * vWidth;
					float vEnd = vStart + vWidth;
					for (int col = 0; col < numCols; ++col) {
						float xStart = xMin + col * xWidth;
						float xEnd = xStart + xWidth;
						float uStart = uMin + col * uWidth;
						float uEnd = uStart + uWidth;
						osg::Node* SUBtile =createTile(heightMap, ss, xStart, xEnd, yStart, yEnd, zMin, zMax, uStart, uEnd, vStart, vEnd);
						tile->addChild(SUBtile);
					}
				}
			}
#endif
#if NEARPIX
			//debug option - don't interpolate
			tex->setFilter(osg::Texture::MIN_FILTER, osg::Texture::NEAREST);//osg::Texture::LINEAR_MIPMAP_LINEAR
			tex->setFilter(osg::Texture::MAG_FILTER, osg::Texture::NEAREST);
#endif
			if (mt) {
				mt->addChild(tile);
				return mt;
			}
			return tile;
		}
		osg::Group *group = mt ? mt : new osg::Group();
		//group->setStateSet(ss);
		group->addChild(createPagedLOD(heightMap, ss, v1, subFileName, fileExt, nextLevel, nextXTile    , nextYTile + 1, xMin, xMid, yMin, yMid, zMin, zMax, uMin, uMid, vMin, vMid));//left bottom
		group->addChild(createPagedLOD(heightMap, ss, v1, subFileName, fileExt, nextLevel, nextXTile + 1, nextYTile + 1, xMid, xMax, yMin, yMid, zMin, zMax, uMid, uMax, vMin, vMid));//right bottom
		group->addChild(createPagedLOD(heightMap, ss, v1, subFileName, fileExt, nextLevel, nextXTile    , nextYTile    , xMin, xMid, yMid, yMax, zMin, zMax, uMin, uMid, vMid, vMax));//left top
		group->addChild(createPagedLOD(heightMap, ss, v1, subFileName, fileExt, nextLevel, nextXTile + 1, nextYTile    , xMid, xMax, yMid, yMax, zMin, zMax, uMid, uMax, vMid, vMax));//right top

		if (luaFile && heightMap) group->setUserData(img);//TODO FREE
		return group;
	}

	virtual ReadResult readNode(const std::string& fileName, const osgDB::ReaderWriter::Options* options) const {
		std::string ext = osgDB::getFileExtension(fileName);
		if (!acceptsExtension(ext)) return ReadResult::FILE_NOT_HANDLED;
		std::string::size_type dot = fileName.find_last_of('.');
		if (dot == std::string::npos) return ReadResult::FILE_NOT_HANDLED;
		std::string subFileName(fileName.begin(), fileName.begin() + dot);
		return readGeodataNode(subFileName, options);
		return ReadResult::FILE_NOT_HANDLED;
	}
};

// now register with Registry to instantiate the above
// reader/writer.
REGISTER_OSGPLUGIN(wmts, ReaderWriterWmts)
