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

/* file:        src/osgPlugins/---/ReaderWriterPTFILTER.cpp
 * author:      Laurens Voerman
 * copyright:   (C) 2021 Laurens Voerman
 * license:     OpenSceneGraph Public License (OSGPL)
*/

#include <osg/Notify>
#include <osg/Matrix>
#include <osg/MatrixTransform>

#include <osgDB/ReaderWriter>
#include <osgDB/FileNameUtils>
#include <osgDB/Registry>
#include <osgDB/ReadFile>
#ifdef DEBUG
#define DBG_WRITE 1
#endif
#if DBG_WRITE
#include <osgDB/WriteFile>
#endif
#include <osg/Version>
#include <osg/Array>

#include <stdio.h>
#include <sstream>
#include <vector>

#define EXTENSION_NAME "ptfilter"
#define RW_NAME ReaderWriterPTFILTER

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
 * An OSG reader plugin for the ".ptfilter" pseudo-loader, which reads a dataset
 * and than filters out points.
 *
 * Usage: <modelfile.ext>.<x>.ptfilter
 * where:
 *      <modelfile.ext> = an image filename.
 *      <x> = filter to apply
 *
 * example: osgviewer Sources-even.ive.[z,0].ptfilter  - remove points with negative z position
 * example: osgviewer Srcs18-oddSE.ive.-#11x,8,+x,0.ptfilter  - filter vertex attribute 11 for only values below 8, remove points with negative x position
 * example: filter: vertex postition: x,y,z
 *     named arrays: ncsf for normal/color/secondcolor/fog, followed by single letter from (rgba), (uv) or (xyz)
 * example: filter: Sources-even.ive.[-nz,0].ptfilter  - remove points with negative z normal
 *     generic vertex attributes: #nn , followed by single letter from (rgba), (uv) or (xyz)
 * compact filter: Srcs18-oddSE.ive.+x,0,compact,1.ptfilter - rewrite attribute arrays skipping the filered values
 */
class pointsFilterVisitor
    : public osg::NodeVisitor
{
    const std::vector<std::string> &filters;
    const std::vector<float> &filterVal;
//    const osgDB::Options* options;
    osg::StateSet* gStateset;
    osg::Matrixf clipBox;
public:
//    pointsFilterVisitor(const std::vector<std::string>& filters, const std::vector<float>& filterVal, const osgDB::Options* opt) : osg::NodeVisitor(osg::NodeVisitor::TRAVERSE_ALL_CHILDREN),
    pointsFilterVisitor(const std::vector<std::string>& filters, const std::vector<float>& filterVal, osg::StateSet* targetStateSet, const osg::Matrixf &clipMat) : osg::NodeVisitor(osg::NodeVisitor::TRAVERSE_ALL_CHILDREN),
        filters(filters),
        filterVal(filterVal),
        gStateset(targetStateSet),
        clipBox(clipMat)
        { }
/*  OPIMIZE- first filter can fill indices w/o reading
    size_t filter_max(osg::DrawElementsUInt& indices, osg::Array &arr, float value) {
        //to access the data i'll need to find the type
        
        size_t j = 0;
        size_t index_end = arr.getNumElements();
        switch (arr.getDataType()) {
            case GL_FLOAT:
                if (osg::MixinVector<GLfloat>* vec = dynamic_cast<osg::MixinVector<GLfloat>*>(&arr)) {
                    for (size_t i = 0; i < index_end; ++i) if ((*vec)[i] < value)  indices[j++] = i;
                }
                break;
            default:
                for (size_t i = 0; i< index_end;++i) indices[i] = i;
                return index_end;
                break;
        }
        return j;
    }
    */
    //refilter_clipmatrix: 
    size_t refilter_clipmatrix(osg::DrawElementsUInt& indices, const osg::Array& arr, const osg::Matrix& mat) {
        size_t j = 0;
        size_t index_end = indices.size();
        switch (arr.getType()) {
        default:
            OSG_WARN << EXTENSION_NAME " vertex array type other than vec3 not supported for clipbox filter." << std::endl;
            j = index_end;//no filter
            break;
        case osg::Array::Vec3ArrayType:
            if (const osg::MixinVector<osg::Vec3>* vec = dynamic_cast<const osg::MixinVector<osg::Vec3>*>(&arr)) {
                for (size_t i = 0; i < index_end; ++i) {
                    osg::Vec3 clipCoords = ((*vec)[indices[i]] * mat);
                    if (clipCoords.x() > 0.0f && clipCoords.x() < 1.0f &&
                        clipCoords.y() > 0.0f && clipCoords.y() < 1.0f &&
                        clipCoords.z() > 0.0f && clipCoords.z() < 1.0f )
                    indices[j++] = indices[i];//pass
                }
            }
            break;
        }
        indices.resize(j);
        return j;
    }
    size_t refilter_max(osg::DrawElementsUInt& indices, const osg::Array& arr, const float filterValue, const size_t filterIndex) {
        size_t j = 0;
        size_t index_end = indices.size();
#define UNIFORM_TYPE_GLuint64 unsigned long long
#define UNIFORM_TYPE_GLint64 long long
#define HANDLE_ARRAY_OF_TYPE1(A,B)  case B##Type: if (filterIndex >  0) j = index_end; else if (const osg::MixinVector<A>* vec = dynamic_cast<const osg::MixinVector<A>*>(&arr)) {for (size_t i = 0; i < index_end; ++i) if ( (*vec)[indices[i]]                 < filterValue)  indices[j++] = indices[i];}break
#define HANDLE_ARRAY_OF_TYPE2(A,B)  case B##Type: if (filterIndex >  1) j = index_end; else if (const osg::MixinVector<A>* vec = dynamic_cast<const osg::MixinVector<A>*>(&arr)) {for (size_t i = 0; i < index_end; ++i) if (((*vec)[indices[i]])[filterIndex]   < filterValue)  indices[j++] = indices[i];}break
#define HANDLE_ARRAY_OF_TYPE3(A,B)  case B##Type: if (filterIndex >  2) j = index_end; else if (const osg::MixinVector<A>* vec = dynamic_cast<const osg::MixinVector<A>*>(&arr)) {for (size_t i = 0; i < index_end; ++i) if (((*vec)[indices[i]])[filterIndex]   < filterValue)  indices[j++] = indices[i];}break
#define HANDLE_ARRAY_OF_TYPE4(A,B)  case B##Type: if (filterIndex >  3) j = index_end; else if (const osg::MixinVector<A>* vec = dynamic_cast<const osg::MixinVector<A>*>(&arr)) {for (size_t i = 0; i < index_end; ++i) if (((*vec)[indices[i]])[filterIndex]   < filterValue)  indices[j++] = indices[i];}break
#define HANDLE_ARRAY_OF_TYPE16(A,B) case B##Type: if (filterIndex >  3) j = index_end; else if (const osg::MixinVector<A>* vec = dynamic_cast<const osg::MixinVector<A>*>(&arr)) {for (size_t i = 0; i < index_end; ++i) if (((*vec)[indices[i]])(3,filterIndex) < filterValue)  indices[j++] = indices[i];}break

        switch (arr.getType()) {

        default:
            j = index_end;//no filter
            break;

            HANDLE_ARRAY_OF_TYPE1(GLbyte, osg::Array::ByteArray);
            HANDLE_ARRAY_OF_TYPE1(GLshort, osg::Array::ShortArray);
            HANDLE_ARRAY_OF_TYPE1(GLint, osg::Array::IntArray);
            HANDLE_ARRAY_OF_TYPE1(GLubyte, osg::Array::UByteArray);
            HANDLE_ARRAY_OF_TYPE1(GLushort, osg::Array::UShortArray);
            HANDLE_ARRAY_OF_TYPE1(GLuint, osg::Array::UIntArray);
            HANDLE_ARRAY_OF_TYPE1(GLfloat, osg::Array::FloatArray);
            HANDLE_ARRAY_OF_TYPE1(GLdouble, osg::Array::DoubleArray);
            HANDLE_ARRAY_OF_TYPE2(osg::Vec2b, osg::Array::Vec2bArray);
            HANDLE_ARRAY_OF_TYPE3(osg::Vec3b, osg::Array::Vec3bArray);
            HANDLE_ARRAY_OF_TYPE4(osg::Vec4b, osg::Array::Vec4bArray);
            HANDLE_ARRAY_OF_TYPE2(osg::Vec2s, osg::Array::Vec2sArray);
            HANDLE_ARRAY_OF_TYPE3(osg::Vec3s, osg::Array::Vec3sArray);
            HANDLE_ARRAY_OF_TYPE4(osg::Vec4s, osg::Array::Vec4sArray);
            HANDLE_ARRAY_OF_TYPE2(osg::Vec2i, osg::Array::Vec2iArray);
            HANDLE_ARRAY_OF_TYPE3(osg::Vec3i, osg::Array::Vec3iArray);
            HANDLE_ARRAY_OF_TYPE4(osg::Vec4i, osg::Array::Vec4iArray);
            HANDLE_ARRAY_OF_TYPE2(osg::Vec2ub, osg::Array::Vec2ubArray);
            HANDLE_ARRAY_OF_TYPE3(osg::Vec3ub, osg::Array::Vec3ubArray);
            HANDLE_ARRAY_OF_TYPE4(osg::Vec4ub, osg::Array::Vec4ubArray);
            HANDLE_ARRAY_OF_TYPE2(osg::Vec2us, osg::Array::Vec2usArray);
            HANDLE_ARRAY_OF_TYPE3(osg::Vec3us, osg::Array::Vec3usArray);
            HANDLE_ARRAY_OF_TYPE4(osg::Vec4us, osg::Array::Vec4usArray);
            HANDLE_ARRAY_OF_TYPE2(osg::Vec2ui, osg::Array::Vec2uiArray);
            HANDLE_ARRAY_OF_TYPE3(osg::Vec3ui, osg::Array::Vec3uiArray);
            HANDLE_ARRAY_OF_TYPE4(osg::Vec4ui, osg::Array::Vec4uiArray);
            HANDLE_ARRAY_OF_TYPE2(osg::Vec2, osg::Array::Vec2Array);
            HANDLE_ARRAY_OF_TYPE3(osg::Vec3, osg::Array::Vec3Array);
            HANDLE_ARRAY_OF_TYPE4(osg::Vec4, osg::Array::Vec4Array);
            HANDLE_ARRAY_OF_TYPE2(osg::Vec2d, osg::Array::Vec2dArray);
            HANDLE_ARRAY_OF_TYPE3(osg::Vec3d, osg::Array::Vec3dArray);
            HANDLE_ARRAY_OF_TYPE4(osg::Vec4d, osg::Array::Vec4dArray);
            HANDLE_ARRAY_OF_TYPE16(osg::Matrixf, osg::Array::MatrixArray);//osg::Array::MatrixfArray
            HANDLE_ARRAY_OF_TYPE16(osg::Matrixd, osg::Array::MatrixdArray);
            HANDLE_ARRAY_OF_TYPE4(osg::Quat, osg::Array::QuatArray);
            HANDLE_ARRAY_OF_TYPE1(UNIFORM_TYPE_GLuint64, osg::Array::UInt64Array);
            HANDLE_ARRAY_OF_TYPE1(UNIFORM_TYPE_GLint64, osg::Array::Int64Array);
        }
        return j;
    }
    size_t refilter_min(osg::DrawElementsUInt& indices, const osg::Array& arr, const float filterValue, const size_t filterIndex) {
        size_t j = 0;
        size_t index_end = indices.size();
#define HANDLE_ARRAY_MIN_TYPE1(A,B)  case B##Type: if (filterIndex >  0) j = index_end; else if (const osg::MixinVector<A>* vec = dynamic_cast<const osg::MixinVector<A>*>(&arr)) {for (size_t i = 0; i < index_end; ++i) if ( (*vec)[indices[i]]                 > filterValue)  indices[j++] = indices[i];}break
#define HANDLE_ARRAY_MIN_TYPE2(A,B)  case B##Type: if (filterIndex >  1) j = index_end; else if (const osg::MixinVector<A>* vec = dynamic_cast<const osg::MixinVector<A>*>(&arr)) {for (size_t i = 0; i < index_end; ++i) if (((*vec)[indices[i]])[filterIndex]   > filterValue)  indices[j++] = indices[i];}break
#define HANDLE_ARRAY_MIN_TYPE3(A,B)  case B##Type: if (filterIndex >  2) j = index_end; else if (const osg::MixinVector<A>* vec = dynamic_cast<const osg::MixinVector<A>*>(&arr)) {for (size_t i = 0; i < index_end; ++i) if (((*vec)[indices[i]])[filterIndex]   > filterValue)  indices[j++] = indices[i];}break
#define HANDLE_ARRAY_MIN_TYPE4(A,B)  case B##Type: if (filterIndex >  3) j = index_end; else if (const osg::MixinVector<A>* vec = dynamic_cast<const osg::MixinVector<A>*>(&arr)) {for (size_t i = 0; i < index_end; ++i) if (((*vec)[indices[i]])[filterIndex]   > filterValue)  indices[j++] = indices[i];}break
#define HANDLE_ARRAY_MIN_TYPE16(A,B) case B##Type: if (filterIndex >  3) j = index_end; else if (const osg::MixinVector<A>* vec = dynamic_cast<const osg::MixinVector<A>*>(&arr)) {for (size_t i = 0; i < index_end; ++i) if (((*vec)[indices[i]])(3,filterIndex) > filterValue)  indices[j++] = indices[i];}break

        switch (arr.getType()) {

        default:
            j = index_end;//no filter
            break;

            HANDLE_ARRAY_MIN_TYPE1(GLbyte, osg::Array::ByteArray);
            HANDLE_ARRAY_MIN_TYPE1(GLshort, osg::Array::ShortArray);
            HANDLE_ARRAY_MIN_TYPE1(GLint, osg::Array::IntArray);
            HANDLE_ARRAY_MIN_TYPE1(GLubyte, osg::Array::UByteArray);
            HANDLE_ARRAY_MIN_TYPE1(GLushort, osg::Array::UShortArray);
            HANDLE_ARRAY_MIN_TYPE1(GLuint, osg::Array::UIntArray);
            HANDLE_ARRAY_MIN_TYPE1(GLfloat, osg::Array::FloatArray);
            HANDLE_ARRAY_MIN_TYPE1(GLdouble, osg::Array::DoubleArray);
            HANDLE_ARRAY_MIN_TYPE2(osg::Vec2b, osg::Array::Vec2bArray);
            HANDLE_ARRAY_MIN_TYPE3(osg::Vec3b, osg::Array::Vec3bArray);
            HANDLE_ARRAY_MIN_TYPE4(osg::Vec4b, osg::Array::Vec4bArray);
            HANDLE_ARRAY_MIN_TYPE2(osg::Vec2s, osg::Array::Vec2sArray);
            HANDLE_ARRAY_MIN_TYPE3(osg::Vec3s, osg::Array::Vec3sArray);
            HANDLE_ARRAY_MIN_TYPE4(osg::Vec4s, osg::Array::Vec4sArray);
            HANDLE_ARRAY_MIN_TYPE2(osg::Vec2i, osg::Array::Vec2iArray);
            HANDLE_ARRAY_MIN_TYPE3(osg::Vec3i, osg::Array::Vec3iArray);
            HANDLE_ARRAY_MIN_TYPE4(osg::Vec4i, osg::Array::Vec4iArray);
            HANDLE_ARRAY_MIN_TYPE2(osg::Vec2ub, osg::Array::Vec2ubArray);
            HANDLE_ARRAY_MIN_TYPE3(osg::Vec3ub, osg::Array::Vec3ubArray);
            HANDLE_ARRAY_MIN_TYPE4(osg::Vec4ub, osg::Array::Vec4ubArray);
            HANDLE_ARRAY_MIN_TYPE2(osg::Vec2us, osg::Array::Vec2usArray);
            HANDLE_ARRAY_MIN_TYPE3(osg::Vec3us, osg::Array::Vec3usArray);
            HANDLE_ARRAY_MIN_TYPE4(osg::Vec4us, osg::Array::Vec4usArray);
            HANDLE_ARRAY_MIN_TYPE2(osg::Vec2ui, osg::Array::Vec2uiArray);
            HANDLE_ARRAY_MIN_TYPE3(osg::Vec3ui, osg::Array::Vec3uiArray);
            HANDLE_ARRAY_MIN_TYPE4(osg::Vec4ui, osg::Array::Vec4uiArray);
            HANDLE_ARRAY_MIN_TYPE2(osg::Vec2, osg::Array::Vec2Array);
            HANDLE_ARRAY_MIN_TYPE3(osg::Vec3, osg::Array::Vec3Array);
            HANDLE_ARRAY_MIN_TYPE4(osg::Vec4, osg::Array::Vec4Array);
            HANDLE_ARRAY_MIN_TYPE2(osg::Vec2d, osg::Array::Vec2dArray);
            HANDLE_ARRAY_MIN_TYPE3(osg::Vec3d, osg::Array::Vec3dArray);
            HANDLE_ARRAY_MIN_TYPE4(osg::Vec4d, osg::Array::Vec4dArray);
            HANDLE_ARRAY_MIN_TYPE16(osg::Matrixf, osg::Array::MatrixArray);//osg::Array::MatrixfArray
            HANDLE_ARRAY_MIN_TYPE16(osg::Matrixd, osg::Array::MatrixdArray);
            HANDLE_ARRAY_MIN_TYPE4(osg::Quat, osg::Array::QuatArray);
            HANDLE_ARRAY_MIN_TYPE1(UNIFORM_TYPE_GLuint64, osg::Array::UInt64Array);
            HANDLE_ARRAY_MIN_TYPE1(UNIFORM_TYPE_GLint64, osg::Array::Int64Array);
        }
        return j;
    }
    void filter_apply(const osg::DrawElementsUInt& indices, osg::Array& arr, const std::string& arrayName, osg::StateSet* gStateset) {
        //to access the data i'll need to find the type
        size_t index_end = indices.size();

        //Rearrange array according to indeces
#define FILTER_ARRAY_OF_TYPE(A,B) case B##Type: if (osg::MixinVector<A>* vec = dynamic_cast<osg::MixinVector<A>*>(&arr)) {for (size_t i = 0; i < index_end; ++i) {\
(*vec)[i] = (*vec)[indices[i] ]; }\
(*vec).resize(index_end);\
}break
//find min/max while rearranging array for simple datatypes
#define FILTER_ARRAY_OF_TYPE1(A,B) case B##Type: if (osg::MixinVector<A>* vec = dynamic_cast<osg::MixinVector<A>*>(&arr)) {\
A minVal = (*vec)[indices[0] ]; \
A maxVal = (*vec)[indices[0] ]; \
for (size_t i = 0; i < index_end; ++i) {\
(*vec)[i] = (*vec)[indices[i] ]; \
if ((*vec)[i] < minVal) minVal = (*vec)[i];\
else if ((*vec)[i] > maxVal) maxVal = (*vec)[i];\
}\
 osg::ref_ptr<osg::Uniform> uniform = new osg::Uniform((arrayName + "_min").c_str(), minVal);\
 gStateset->addUniform(uniform);\
 uniform = new osg::Uniform((arrayName + "_width").c_str(), maxVal - minVal);\
 gStateset->addUniform(uniform);\
(*vec).resize(index_end);\
}break
//find min/max while rearranging array for vector datatypes
#define FILTER_ARRAY_OF_TYPEM(A,B) case B##Type: if (osg::MixinVector<A>* vec = dynamic_cast<osg::MixinVector<A>*>(&arr)) {\
A minVal = (*vec)[indices[0] ]; \
A maxVal = (*vec)[indices[0] ]; \
for (size_t i = 0; i < index_end; ++i) {\
(*vec)[i] = (*vec)[indices[i] ]; \
for (int j = 0; j<arr.getDataSize();++j) \
if ((*vec)[i][j] < minVal[j]) minVal[j] = (*vec)[i][j];\
else if ((*vec)[i][j] > maxVal[j]) maxVal[j] = (*vec)[i][j];\
}\
 osg::ref_ptr<osg::Uniform> uniform = new osg::Uniform((arrayName + "_min").c_str(), minVal);\
 gStateset->addUniform(uniform);\
 uniform = new osg::Uniform((arrayName + "_width").c_str(), maxVal - minVal);\
 gStateset->addUniform(uniform);\
(*vec).resize(index_end);\
}break
//find min/max while rearranging array for non float vector datatypes 
#define FILTER_ARRAY_OF_TYPEMF(A,B,C) case B##Type: if (osg::MixinVector<A>* vec = dynamic_cast<osg::MixinVector<A>*>(&arr)) {\
C minVal; \
C maxVal; \
for (int j = 0; j<arr.getDataSize();++j) {\
minVal[j] = (*vec)[indices[0] ][j];\
maxVal[j] = (*vec)[indices[0] ][j];\
}\
for (size_t i = 0; i < index_end; ++i) {\
(*vec)[i] = (*vec)[indices[i] ]; \
for (int j = 0; j<arr.getDataSize();++j) \
if ((*vec)[i][j] < minVal[j]) minVal[j] = (*vec)[i][j];\
else if ((*vec)[i][j] > maxVal[j]) maxVal[j] = (*vec)[i][j];\
}\
 osg::ref_ptr<osg::Uniform> uniform = new osg::Uniform((arrayName + "_min").c_str(), minVal);\
 gStateset->addUniform(uniform);\
 uniform = new osg::Uniform((arrayName + "_width").c_str(), maxVal - minVal);\
 gStateset->addUniform(uniform);\
(*vec).resize(index_end);\
}break

        //arr.getDataSize() 1 for simpletypes; 2 for vec2? ; 3/4 for vec3/4 or 16 for matrix
        //arr.getDataType() GL_BYTE - _SHORT/_INT GL_FLOAT OR GL_DOUBLE
        switch (arr.getType()) {
            FILTER_ARRAY_OF_TYPE1(GLbyte, osg::Array::ByteArray);
            FILTER_ARRAY_OF_TYPE1(GLshort, osg::Array::ShortArray);
            FILTER_ARRAY_OF_TYPE1(GLint, osg::Array::IntArray);
            FILTER_ARRAY_OF_TYPE1(GLubyte, osg::Array::UByteArray);
            FILTER_ARRAY_OF_TYPE1(GLushort, osg::Array::UShortArray);
            FILTER_ARRAY_OF_TYPE1(GLuint, osg::Array::UIntArray);
            FILTER_ARRAY_OF_TYPE1(GLfloat, osg::Array::FloatArray);
            FILTER_ARRAY_OF_TYPE1(GLdouble, osg::Array::DoubleArray);
            FILTER_ARRAY_OF_TYPEMF(osg::Vec2b, osg::Array::Vec2bArray, osg::Vec2);
            FILTER_ARRAY_OF_TYPEMF(osg::Vec3b, osg::Array::Vec3bArray, osg::Vec3);
            FILTER_ARRAY_OF_TYPEMF(osg::Vec4b, osg::Array::Vec4bArray, osg::Vec4);
            FILTER_ARRAY_OF_TYPEMF(osg::Vec2s, osg::Array::Vec2sArray, osg::Vec2);
            FILTER_ARRAY_OF_TYPEMF(osg::Vec3s, osg::Array::Vec3sArray, osg::Vec3);
            FILTER_ARRAY_OF_TYPEMF(osg::Vec4s, osg::Array::Vec4sArray, osg::Vec4);
            FILTER_ARRAY_OF_TYPEMF(osg::Vec2i, osg::Array::Vec2iArray, osg::Vec2);
            FILTER_ARRAY_OF_TYPEMF(osg::Vec3i, osg::Array::Vec3iArray, osg::Vec3);
            FILTER_ARRAY_OF_TYPEMF(osg::Vec4i, osg::Array::Vec4iArray, osg::Vec4);
            FILTER_ARRAY_OF_TYPEMF(osg::Vec2ub, osg::Array::Vec2ubArray, osg::Vec2);
            FILTER_ARRAY_OF_TYPEMF(osg::Vec3ub, osg::Array::Vec3ubArray, osg::Vec3);
            FILTER_ARRAY_OF_TYPEMF(osg::Vec4ub, osg::Array::Vec4ubArray, osg::Vec4);
            FILTER_ARRAY_OF_TYPEMF(osg::Vec2us, osg::Array::Vec2usArray, osg::Vec2);
            FILTER_ARRAY_OF_TYPEMF(osg::Vec3us, osg::Array::Vec3usArray, osg::Vec3);
            FILTER_ARRAY_OF_TYPEMF(osg::Vec4us, osg::Array::Vec4usArray, osg::Vec4);
            FILTER_ARRAY_OF_TYPEMF(osg::Vec2ui, osg::Array::Vec2uiArray, osg::Vec2);
            FILTER_ARRAY_OF_TYPEMF(osg::Vec3ui, osg::Array::Vec3uiArray, osg::Vec3);
            FILTER_ARRAY_OF_TYPEMF(osg::Vec4ui, osg::Array::Vec4uiArray, osg::Vec4);
            FILTER_ARRAY_OF_TYPEM(osg::Vec2, osg::Array::Vec2Array);
            FILTER_ARRAY_OF_TYPEM(osg::Vec3, osg::Array::Vec3Array);
            FILTER_ARRAY_OF_TYPEM(osg::Vec4, osg::Array::Vec4Array);
            FILTER_ARRAY_OF_TYPEM(osg::Vec2d, osg::Array::Vec2dArray);
            FILTER_ARRAY_OF_TYPEM(osg::Vec3d, osg::Array::Vec3dArray);
            FILTER_ARRAY_OF_TYPEM(osg::Vec4d, osg::Array::Vec4dArray);
            FILTER_ARRAY_OF_TYPE(osg::Matrixf, osg::Array::MatrixArray);//osg::Array::MatrixfArray
            FILTER_ARRAY_OF_TYPE(osg::Matrixd, osg::Array::MatrixdArray);
            FILTER_ARRAY_OF_TYPE(osg::Quat, osg::Array::QuatArray);
            FILTER_ARRAY_OF_TYPE1(UNIFORM_TYPE_GLuint64, osg::Array::UInt64Array);
            FILTER_ARRAY_OF_TYPE1(UNIFORM_TYPE_GLint64, osg::Array::Int64Array);
        default:
            break;
        }
    }

    //minmaxfilter_applyI: just find min / max values (in indexed array); don't change array
    void minmaxfilter_applyI(const osg::DrawElementsUInt& indices, const osg::Array& arr, std::string& arrayName, osg::StateSet* gStateset) {

        size_t index_end = indices.size();//to access the data i'll need to find the type
        //don't Rearrange array according to indices
#define MINMAX_ARRAYI_OF_TYPE(A,B) case B##Type: break
//find min/max while not rearranging array for simple datatypes
#define MINMAX_ARRAYI_OF_TYPE1(A,B) case B##Type: if (const osg::MixinVector<A>* vec = dynamic_cast<const osg::MixinVector<A>*>(&arr)) {\
A minVal = (*vec)[indices[0]]; \
A maxVal = (*vec)[indices[0]]; \
for (size_t i = 0; i < index_end; ++i) {\
if ((*vec)[indices[i]] < minVal) minVal = (*vec)[indices[i]];\
else if ((*vec)[indices[i]] > maxVal) maxVal = (*vec)[indices[i]];\
}\
 osg::ref_ptr<osg::Uniform> uniform = new osg::Uniform((arrayName + "_min").c_str(), minVal);\
 gStateset->addUniform(uniform);\
 uniform = new osg::Uniform((arrayName + "_width").c_str(), maxVal - minVal);\
 gStateset->addUniform(uniform);\
}break
//find min/max while not rearranging array for vector datatypes
#define MINMAX_ARRAYI_OF_TYPEM(A,B) case B##Type: if (const osg::MixinVector<A>* vec = dynamic_cast<const osg::MixinVector<A>*>(&arr)) {\
A minVal = (*vec)[indices[0]]; \
A maxVal = (*vec)[indices[0]]; \
for (size_t i = 0; i < index_end; ++i) {\
for (int j = 0; j<arr.getDataSize();++j) \
if ((*vec)[indices[i]][j] < minVal[j]) minVal[j] = (*vec)[indices[i]][j];\
else if ((*vec)[indices[i]][j] > maxVal[j]) maxVal[j] = (*vec)[indices[i]][j];\
}\
 osg::ref_ptr<osg::Uniform> uniform = new osg::Uniform((arrayName + "_min").c_str(), minVal);\
 gStateset->addUniform(uniform);\
 uniform = new osg::Uniform((arrayName + "_width").c_str(), maxVal - minVal);\
 gStateset->addUniform(uniform);\
}break
//find min/max while not rearranging array for non float vector datatypes 
#define MINMAX_ARRAYI_OF_TYPEMF(A,B,C) case B##Type: if (const osg::MixinVector<A>* vec = dynamic_cast<const osg::MixinVector<A>*>(&arr)) {\
C minVal; \
C maxVal; \
for (int j = 0; j<arr.getDataSize();++j) {\
minVal[j] = (*vec)[indices[0]][j];\
maxVal[j] = (*vec)[indices[0]][j];\
}\
for (size_t i = 0; i < index_end; ++i) {\
for (int j = 0; j<arr.getDataSize();++j) \
if ((*vec)[indices[i]][j] < minVal[j]) minVal[j] = (*vec)[indices[i]][j];\
else if ((*vec)[indices[i]][j] > maxVal[j]) maxVal[j] = (*vec)[indices[i]][j];\
}\
 osg::ref_ptr<osg::Uniform> uniform = new osg::Uniform((arrayName + "_min").c_str(), minVal);\
 gStateset->addUniform(uniform);\
 uniform = new osg::Uniform((arrayName + "_width").c_str(), maxVal - minVal);\
 gStateset->addUniform(uniform);\
}break

        //arr.getDataSize() 1 for simpletypes; 2 for vec2? ; 3/4 for vec3/4 or 16 for matrix
        //arr.getDataType() GL_BYTE - _SHORT/_INT GL_FLOAT OR GL_DOUBLE
        switch (arr.getType()) {
            MINMAX_ARRAYI_OF_TYPE1(GLbyte, osg::Array::ByteArray);
            MINMAX_ARRAYI_OF_TYPE1(GLshort, osg::Array::ShortArray);
            MINMAX_ARRAYI_OF_TYPE1(GLint, osg::Array::IntArray);
            MINMAX_ARRAYI_OF_TYPE1(GLubyte, osg::Array::UByteArray);
            MINMAX_ARRAYI_OF_TYPE1(GLushort, osg::Array::UShortArray);
            MINMAX_ARRAYI_OF_TYPE1(GLuint, osg::Array::UIntArray);
            MINMAX_ARRAYI_OF_TYPE1(GLfloat, osg::Array::FloatArray);
            MINMAX_ARRAYI_OF_TYPE1(GLdouble, osg::Array::DoubleArray);
            MINMAX_ARRAYI_OF_TYPEMF(osg::Vec2b, osg::Array::Vec2bArray, osg::Vec2);
            MINMAX_ARRAYI_OF_TYPEMF(osg::Vec3b, osg::Array::Vec3bArray, osg::Vec3);
            MINMAX_ARRAYI_OF_TYPEMF(osg::Vec4b, osg::Array::Vec4bArray, osg::Vec4);
            MINMAX_ARRAYI_OF_TYPEMF(osg::Vec2s, osg::Array::Vec2sArray, osg::Vec2);
            MINMAX_ARRAYI_OF_TYPEMF(osg::Vec3s, osg::Array::Vec3sArray, osg::Vec3);
            MINMAX_ARRAYI_OF_TYPEMF(osg::Vec4s, osg::Array::Vec4sArray, osg::Vec4);
            MINMAX_ARRAYI_OF_TYPEMF(osg::Vec2i, osg::Array::Vec2iArray, osg::Vec2);
            MINMAX_ARRAYI_OF_TYPEMF(osg::Vec3i, osg::Array::Vec3iArray, osg::Vec3);
            MINMAX_ARRAYI_OF_TYPEMF(osg::Vec4i, osg::Array::Vec4iArray, osg::Vec4);
            MINMAX_ARRAYI_OF_TYPEMF(osg::Vec2ub, osg::Array::Vec2ubArray, osg::Vec2);
            MINMAX_ARRAYI_OF_TYPEMF(osg::Vec3ub, osg::Array::Vec3ubArray, osg::Vec3);
            MINMAX_ARRAYI_OF_TYPEMF(osg::Vec4ub, osg::Array::Vec4ubArray, osg::Vec4);
            MINMAX_ARRAYI_OF_TYPEMF(osg::Vec2us, osg::Array::Vec2usArray, osg::Vec2);
            MINMAX_ARRAYI_OF_TYPEMF(osg::Vec3us, osg::Array::Vec3usArray, osg::Vec3);
            MINMAX_ARRAYI_OF_TYPEMF(osg::Vec4us, osg::Array::Vec4usArray, osg::Vec4);
            MINMAX_ARRAYI_OF_TYPEMF(osg::Vec2ui, osg::Array::Vec2uiArray, osg::Vec2);
            MINMAX_ARRAYI_OF_TYPEMF(osg::Vec3ui, osg::Array::Vec3uiArray, osg::Vec3);
            MINMAX_ARRAYI_OF_TYPEMF(osg::Vec4ui, osg::Array::Vec4uiArray, osg::Vec4);
            MINMAX_ARRAYI_OF_TYPEM(osg::Vec2, osg::Array::Vec2Array);
            MINMAX_ARRAYI_OF_TYPEM(osg::Vec3, osg::Array::Vec3Array);
            MINMAX_ARRAYI_OF_TYPEM(osg::Vec4, osg::Array::Vec4Array);
            MINMAX_ARRAYI_OF_TYPEM(osg::Vec2d, osg::Array::Vec2dArray);
            MINMAX_ARRAYI_OF_TYPEM(osg::Vec3d, osg::Array::Vec3dArray);
            MINMAX_ARRAYI_OF_TYPEM(osg::Vec4d, osg::Array::Vec4dArray);
            MINMAX_ARRAYI_OF_TYPE(osg::Matrixf, osg::Array::MatrixArray);//osg::Array::MatrixfArray
            MINMAX_ARRAYI_OF_TYPE(osg::Matrixd, osg::Array::MatrixdArray);
            MINMAX_ARRAYI_OF_TYPE(osg::Quat, osg::Array::QuatArray);
            MINMAX_ARRAYI_OF_TYPE1(UNIFORM_TYPE_GLuint64, osg::Array::UInt64Array);
            MINMAX_ARRAYI_OF_TYPE1(UNIFORM_TYPE_GLint64, osg::Array::Int64Array);
        default:
            break;
        }
    }
    //minmaxfilter_apply: just find min / max values; don't change array
    void minmaxfilter_apply(const size_t index_end, const osg::Array& arr, std::string& arrayName, osg::StateSet* gStateset) {
        
        //size_t index_end = indices.size();//to access the data i'll need to find the type
        //don't Rearrange array according to indices
#define MINMAX_ARRAY_OF_TYPE(A,B) case B##Type: break
//find min/max while not rearranging array for simple datatypes
#define MINMAX_ARRAY_OF_TYPE1(A,B) case B##Type: if (const osg::MixinVector<A>* vec = dynamic_cast<const osg::MixinVector<A>*>(&arr)) {\
A minVal = (*vec)[0]; \
A maxVal = (*vec)[0]; \
for (size_t i = 0; i < index_end; ++i) {\
if ((*vec)[i] < minVal) minVal = (*vec)[i];\
else if ((*vec)[i] > maxVal) maxVal = (*vec)[i];\
}\
 osg::ref_ptr<osg::Uniform> uniform = new osg::Uniform((arrayName + "_min").c_str(), minVal);\
 gStateset->addUniform(uniform);\
 uniform = new osg::Uniform((arrayName + "_width").c_str(), maxVal - minVal);\
 gStateset->addUniform(uniform);\
}break
//find min/max while not rearranging array for vector datatypes
#define MINMAX_ARRAY_OF_TYPEM(A,B) case B##Type: if (const osg::MixinVector<A>* vec = dynamic_cast<const osg::MixinVector<A>*>(&arr)) {\
A minVal = (*vec)[0]; \
A maxVal = (*vec)[0]; \
for (size_t i = 0; i < index_end; ++i) {\
for (int j = 0; j<arr.getDataSize();++j) \
if ((*vec)[i][j] < minVal[j]) minVal[j] = (*vec)[i][j];\
else if ((*vec)[i][j] > maxVal[j]) maxVal[j] = (*vec)[i][j];\
}\
 osg::ref_ptr<osg::Uniform> uniform = new osg::Uniform((arrayName + "_min").c_str(), minVal);\
 gStateset->addUniform(uniform);\
 uniform = new osg::Uniform((arrayName + "_width").c_str(), maxVal - minVal);\
 gStateset->addUniform(uniform);\
}break
//find min/max while not rearranging array for non float vector datatypes 
#define MINMAX_ARRAY_OF_TYPEMF(A,B,C) case B##Type: if (const osg::MixinVector<A>* vec = dynamic_cast<const osg::MixinVector<A>*>(&arr)) {\
C minVal; \
C maxVal; \
for (int j = 0; j<arr.getDataSize();++j) {\
minVal[j] = (*vec)[0][j];\
maxVal[j] = (*vec)[0][j];\
}\
for (size_t i = 0; i < index_end; ++i) {\
for (int j = 0; j<arr.getDataSize();++j) \
if ((*vec)[i][j] < minVal[j]) minVal[j] = (*vec)[i][j];\
else if ((*vec)[i][j] > maxVal[j]) maxVal[j] = (*vec)[i][j];\
}\
 osg::ref_ptr<osg::Uniform> uniform = new osg::Uniform((arrayName + "_min").c_str(), minVal);\
 gStateset->addUniform(uniform);\
 uniform = new osg::Uniform((arrayName + "_width").c_str(), maxVal - minVal);\
 gStateset->addUniform(uniform);\
}break

        //arr.getDataSize() 1 for simpletypes; 2 for vec2? ; 3/4 for vec3/4 or 16 for matrix
        //arr.getDataType() GL_BYTE - _SHORT/_INT GL_FLOAT OR GL_DOUBLE
        switch (arr.getType()) {
            MINMAX_ARRAY_OF_TYPE1(GLbyte, osg::Array::ByteArray);
            MINMAX_ARRAY_OF_TYPE1(GLshort, osg::Array::ShortArray);
            MINMAX_ARRAY_OF_TYPE1(GLint, osg::Array::IntArray);
            MINMAX_ARRAY_OF_TYPE1(GLubyte, osg::Array::UByteArray);
            MINMAX_ARRAY_OF_TYPE1(GLushort, osg::Array::UShortArray);
            MINMAX_ARRAY_OF_TYPE1(GLuint, osg::Array::UIntArray);
            MINMAX_ARRAY_OF_TYPE1(GLfloat, osg::Array::FloatArray);
            MINMAX_ARRAY_OF_TYPE1(GLdouble, osg::Array::DoubleArray);
            MINMAX_ARRAY_OF_TYPEMF(osg::Vec2b, osg::Array::Vec2bArray, osg::Vec2);
            MINMAX_ARRAY_OF_TYPEMF(osg::Vec3b, osg::Array::Vec3bArray, osg::Vec3);
            MINMAX_ARRAY_OF_TYPEMF(osg::Vec4b, osg::Array::Vec4bArray, osg::Vec4);
            MINMAX_ARRAY_OF_TYPEMF(osg::Vec2s, osg::Array::Vec2sArray, osg::Vec2);
            MINMAX_ARRAY_OF_TYPEMF(osg::Vec3s, osg::Array::Vec3sArray, osg::Vec3);
            MINMAX_ARRAY_OF_TYPEMF(osg::Vec4s, osg::Array::Vec4sArray, osg::Vec4);
            MINMAX_ARRAY_OF_TYPEMF(osg::Vec2i, osg::Array::Vec2iArray, osg::Vec2);
            MINMAX_ARRAY_OF_TYPEMF(osg::Vec3i, osg::Array::Vec3iArray, osg::Vec3);
            MINMAX_ARRAY_OF_TYPEMF(osg::Vec4i, osg::Array::Vec4iArray, osg::Vec4);
            MINMAX_ARRAY_OF_TYPEMF(osg::Vec2ub, osg::Array::Vec2ubArray, osg::Vec2);
            MINMAX_ARRAY_OF_TYPEMF(osg::Vec3ub, osg::Array::Vec3ubArray, osg::Vec3);
            MINMAX_ARRAY_OF_TYPEMF(osg::Vec4ub, osg::Array::Vec4ubArray, osg::Vec4);
            MINMAX_ARRAY_OF_TYPEMF(osg::Vec2us, osg::Array::Vec2usArray, osg::Vec2);
            MINMAX_ARRAY_OF_TYPEMF(osg::Vec3us, osg::Array::Vec3usArray, osg::Vec3);
            MINMAX_ARRAY_OF_TYPEMF(osg::Vec4us, osg::Array::Vec4usArray, osg::Vec4);
            MINMAX_ARRAY_OF_TYPEMF(osg::Vec2ui, osg::Array::Vec2uiArray, osg::Vec2);
            MINMAX_ARRAY_OF_TYPEMF(osg::Vec3ui, osg::Array::Vec3uiArray, osg::Vec3);
            MINMAX_ARRAY_OF_TYPEMF(osg::Vec4ui, osg::Array::Vec4uiArray, osg::Vec4);
            MINMAX_ARRAY_OF_TYPEM(osg::Vec2, osg::Array::Vec2Array);
            MINMAX_ARRAY_OF_TYPEM(osg::Vec3, osg::Array::Vec3Array);
            MINMAX_ARRAY_OF_TYPEM(osg::Vec4, osg::Array::Vec4Array);
            MINMAX_ARRAY_OF_TYPEM(osg::Vec2d, osg::Array::Vec2dArray);
            MINMAX_ARRAY_OF_TYPEM(osg::Vec3d, osg::Array::Vec3dArray);
            MINMAX_ARRAY_OF_TYPEM(osg::Vec4d, osg::Array::Vec4dArray);
            MINMAX_ARRAY_OF_TYPE(osg::Matrixf, osg::Array::MatrixArray);//osg::Array::MatrixfArray
            MINMAX_ARRAY_OF_TYPE(osg::Matrixd, osg::Array::MatrixdArray);
            MINMAX_ARRAY_OF_TYPE(osg::Quat, osg::Array::QuatArray);
            MINMAX_ARRAY_OF_TYPE1(UNIFORM_TYPE_GLuint64, osg::Array::UInt64Array);
            MINMAX_ARRAY_OF_TYPE1(UNIFORM_TYPE_GLint64, osg::Array::Int64Array);
        default:
            break;
        }
    }
    virtual void apply(osg::Geometry& geometry) {
        bool filter = true;
        size_t numVertices = geometry.getVertexArray()->getNumElements();
        unsigned int np = geometry.getNumPrimitiveSets();
        if (np != 1) filter = false;
        for (unsigned int p = 0; p < np; ++p) {
            osg::PrimitiveSet* ps = geometry.getPrimitiveSet(p);
            if (ps->getMode() != GL_POINTS) filter = false;
            if (ps->getType() != osg::PrimitiveSet::DrawElementsUIntPrimitiveType) {
                if (ps->getType() != osg::PrimitiveSet::DrawArraysPrimitiveType) {
                    filter = false;//maybe add later: allow my own output osg::DrawElementsUInt(GL_POINTS, numVertices);
                } else {
                    osg::DrawArrays* da = dynamic_cast<osg::DrawArrays*>(ps);
                    numVertices = da->getCount() - da->getFirst();
                }
            } else {
                osg::DrawElementsUInt* deui = dynamic_cast<osg::DrawElementsUInt*>(ps);
                numVertices = deui->size();
            }
        }
        if (numVertices < 1) filter = false;
        if (!filter) {
            OSG_WARN << "ptfilter skipped for geometry" << std::endl;
            if (np != 1) { OSG_WARN << "    geometry.getNumPrimitiveSets() > 1" << std::endl; }
            for (unsigned int p = 0; p < np; ++p) {
                osg::PrimitiveSet* ps = geometry.getPrimitiveSet(p);
                if (ps->getMode() != GL_POINTS) { OSG_WARN << "    geometry.getPrimitiveSet(" << p << ")->getMode() != GL_POINTS" << std::endl;}
                if (ps->getType() != osg::PrimitiveSet::DrawArraysPrimitiveType) { OSG_WARN << "    geometry.getPrimitiveSet(" << p << ")getType() != osg::PrimitiveSet::DrawArraysPrimitiveType" << std::endl; }
            }
            if (numVertices < 1) { OSG_WARN << "    geometry has no vertices!" << std::endl; }
            traverse(geometry);
            return;
        }
        
        std::vector<osg::Array*> pvArrays;//per vertex arrays
        std::vector<std::string> pvArraysNames;
        // gather all per vertex arrays...
        {
            osg::Array* a;
            pvArrays.push_back(geometry.getVertexArray());
            pvArraysNames.push_back("vertex");
            for (unsigned int ti = 0; ti < geometry.getNumTexCoordArrays(); ++ti)
            {
                osg::Array* tc = geometry.getTexCoordArray(ti);
                if (tc && (tc->getBinding() == osg::Array::BIND_PER_VERTEX)) {
                    pvArrays.push_back(tc);
                    std::stringstream tcArrayName;
                    tcArrayName << "tc" << ti;
                    pvArraysNames.push_back(tcArrayName.str());
                }
            }
            a = geometry.getNormalArray();
            if (a && (a->getBinding() == osg::Array::BIND_PER_VERTEX)) { pvArrays.push_back(a); pvArraysNames.push_back("normal"); }
            a = geometry.getColorArray();
            if (a && (a->getBinding() == osg::Array::BIND_PER_VERTEX)) { pvArrays.push_back(a); pvArraysNames.push_back("color"); }
            a = geometry.getSecondaryColorArray();
            if (a && (a->getBinding() == osg::Array::BIND_PER_VERTEX)) { pvArrays.push_back(a); pvArraysNames.push_back("secondcolor"); }
            a = geometry.getFogCoordArray();
            if (a && (a->getBinding() == osg::Array::BIND_PER_VERTEX)) { pvArrays.push_back(a); pvArraysNames.push_back("fog"); }

            for (unsigned int vi = 0; vi < geometry.getNumVertexAttribArrays(); ++vi)
            {
                a = geometry.getVertexAttribArray(vi);
                if (a && (a->getBinding() == osg::Array::BIND_PER_VERTEX)) {
                    pvArrays.push_back(a);
                    std::stringstream genericArrayName;
                    genericArrayName << "ga" << vi;
                    pvArraysNames.push_back(genericArrayName.str());
                }
            }
            // done gathering per vertex arrays...
        }
        {
            osg::DrawElementsUInt* indices = NULL;
            if (geometry.getPrimitiveSet(0)->getType() == osg::PrimitiveSet::DrawElementsUIntPrimitiveType) {
                osg::DrawElementsUInt* deui = dynamic_cast<osg::DrawElementsUInt*>(geometry.getPrimitiveSetList()[0].get());
                indices = new osg::DrawElementsUInt(*deui);
            }
            if (geometry.getPrimitiveSet(0)->getType() == osg::PrimitiveSet::DrawArraysPrimitiveType) {
                osg::DrawArrays* da = dynamic_cast<osg::DrawArrays*>(geometry.getPrimitiveSetList()[0].get());
                unsigned int vCount = da->getCount() - da->getFirst();
                indices = new osg::DrawElementsUInt(GL_POINTS, vCount);
                for (size_t i = 0; i < vCount; ++i) (*indices)[i] = i + da->getFirst();
            }
            if (!indices) {
                OSG_WARN << EXTENSION_NAME "unexpected error with primitive type!" << std::endl;
                indices = new osg::DrawElementsUInt(GL_POINTS, numVertices);
                for (size_t i = 0; i < numVertices; ++i) (*indices)[i] = i ;
            }
            for (size_t filterIndex = 0; filterIndex < filters.size(); ++filterIndex) {
                std::string filterName = filters[filterIndex];
                bool boxFilter = false;
//                osg::Matrix clipBox;
                osg::Array* a = NULL; //array to filter
                if (filterName == "clipbox") {
#if 1
                    boxFilter = true;
                    a = geometry.getVertexArray();
#else
                    osg::Uniform* u = gStateset->getUniform("clipbox");//needs different input
                    if (u) {
                        if (u->get(clipBox)) {
                            OSG_WARN << EXTENSION_NAME " filtering by clipbox uniform." << std::endl;
                            boxFilter = true;
                            a = geometry.getVertexArray();
                        }
                        else {
                            OSG_WARN << EXTENSION_NAME " getUniform(\"clipbox\")->get(mat) failed." << std::endl;
                        }
                    }
                    else {
                        OSG_WARN << EXTENSION_NAME " not filtering by clipbox; options->getParentGroup()->getStateSet()->getUniform(\"clipbox\") == NULL." << std::endl;
                    }
//                    continue; // next filter; ignore filterVal[filterIndex]
#endif
                }

                bool maxFilter = false;
                bool minFilter = false;
                size_t elementIndex = 0; //emement of vector attribute to min/max filter on. 
                if (!boxFilter) {
                    maxFilter = (filterName[0] == '-');//REMOVE if value above treshold
                    minFilter = (filterName[0] == '+');
                    if (maxFilter || minFilter) filterName = filterName.substr(1);
                    else maxFilter = true;
                    if (filterName.empty()) {
                        OSG_WARN << "filter #" << filterIndex << " (value " << filterVal[filterIndex] << ") parse error;  ignoring filter." << std::endl;
                        continue;
                    }
                    int arrayIndex = 0;
                    switch (filterName[0]) {
                    case 'x': { a = geometry.getVertexArray();         elementIndex = 0; break; }
                    case 'y': { a = geometry.getVertexArray();         elementIndex = 1; break; }
                    case 'z': { a = geometry.getVertexArray();         elementIndex = 2; break; }
                    case 'n': { a = geometry.getNormalArray();         elementIndex = 33; break; }
                    case 'c': { a = geometry.getColorArray();          elementIndex = 33; break; }
                    case 's': { a = geometry.getSecondaryColorArray(); elementIndex = 33; break; }
                    case 'f': { a = geometry.getFogCoordArray();       elementIndex = 33; break; }
                    case 't': { if (sscanf(filterName.substr(1).c_str(), "%d", &arrayIndex) == 1) a = geometry.getTexCoordArray(arrayIndex);     elementIndex = 33; break; }
                    case '#': { if (sscanf(filterName.substr(1).c_str(), "%d", &arrayIndex) == 1) a = geometry.getVertexAttribArray(arrayIndex); elementIndex = 33; break; }
                    }
                    if (elementIndex == 33) {
                        switch (filterName[filterName.size() - 1]) {//last char
                        case 'r': {elementIndex = 0; break; }
                        case 'g': {elementIndex = 1; break; }
                        case 'b': {elementIndex = 2; break; }
                        case 'a': {elementIndex = 3; break; }
                        case 'u': {elementIndex = 0; break; }
                        case 'v': {elementIndex = 1; break; }
                        case 'x': {elementIndex = 0; break; }
                        case 'y': {elementIndex = 1; break; }
                        case 'z': {elementIndex = 2; break; }
                        default: {elementIndex = 0; break; }
                        }
                    }
                    if (a == NULL) {
                        OSG_WARN << "filter #" << filterIndex << " (" << filterName << " value " << filterVal[filterIndex] << ") parse error;  ignoring filter." << std::endl;
                        continue;
                    }
                }//!boxFilter
                if (a != NULL) {
                    size_t remaining = 0;
                    if (maxFilter) {
                        remaining = refilter_max((*indices), *a, filterVal[filterIndex], elementIndex);
                    } else {
                        if (minFilter) {
                            remaining = refilter_min((*indices), *a, filterVal[filterIndex], elementIndex);
                        } else {
                            if (boxFilter) {
                                remaining = refilter_clipmatrix((*indices), *a, clipBox);
                            }
                        }
                    }
                    if (remaining != (*indices).size()) {//points removed...
                        (*indices).resize(remaining);
                    }
                }
            }//for each filter
            
            if (compactArrays) {
                for (unsigned int vi = 0; vi < pvArrays.size(); ++vi) {
                    filter_apply((*indices), (*pvArrays[vi]), pvArraysNames[vi], gStateset);
                }

                osg::PrimitiveSet* ps = geometry.getPrimitiveSet(0);
                osg::DrawArrays* da = dynamic_cast<osg::DrawArrays*>(ps);
                da->setCount((*indices).size());
            } else {
                    for (unsigned int vi = 0; vi < pvArrays.size(); ++vi) {
                        if (indices->size() > 0) {//any points left over?
                            minmaxfilter_applyI((*indices), (*pvArrays[vi]), pvArraysNames[vi], gStateset);
                        }
                    }
#if 0
                    float minVal, width;
                    gStateset->getUniform("ga10_min")->get(minVal);
                    gStateset->getUniform("ga10_width")->get(width);
                    OSG_WARN << EXTENSION_NAME "ga10_min" << minVal << " width " << width << " COUNT "<< indices->size() << std::endl;
                    geometry.setPrimitiveSet(0, indices);
#else
                    geometry.setPrimitiveSet(0, indices);
#endif
 //               }
            }
        }

        traverse(geometry);
    }
    virtual void apply(osg::Node& node) { traverse(node); }
    bool compactArrays = false;//remove unused data
};

class RW_NAME : public osgDB::ReaderWriter
{
public:
    RW_NAME()
    {
        supportsExtension(EXTENSION_NAME,"ptfilter Pseudo loader.");
    }

    virtual const char* className() const { return "ptfilter pseudo-loader"; }

    virtual ReadResult readImage(const std::string& fileName, const osgDB::ReaderWriter::Options* options) const
    {
        std::string ext = osgDB::getLowerCaseFileExtension(fileName);
        if( !acceptsExtension(ext) )
            return ReadResult::FILE_NOT_HANDLED;

        OSG_INFO << "RW_NAME( \"" << fileName << "\" )" << std::endl;

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

        OSG_WARN << "Ignoring params while loading image with " EXTENSION_NAME " pseudo-loader" << std::endl;

        // recursively load the subfile.
        return osgDB::readRefImageFile( subFileName, options );
    }
    virtual ReadResult readNode(const std::string& fileName, const osgDB::ReaderWriter::Options* options) const {
        if (!acceptsExtension(osgDB::getLowerCaseFileExtension(fileName))) return ReadResult::FILE_NOT_HANDLED;
        std::string tmpName = osgDB::getNameLessExtension(fileName);
        if (tmpName.empty()) return ReadResult::FILE_NOT_HANDLED;

        std::string subFileName, params;
        if (!getFilenameAndParams(tmpName, subFileName, params)) return ReadResult::FILE_NOT_HANDLED;
        //parse params
        std::vector<std::string> filters;
        std::vector<float> filterVal;
        osg::Matrixf clipboxMat;
        bool makeCompactArrays = false;
        size_t nameStart = 0;
        size_t nameEnd = params.find_first_of(',');
        while (nameEnd != std::string::npos) {
            size_t val_end = params.find_first_of(',', nameEnd + 1);
            std::string currertFilter = params.substr(nameStart, nameEnd - nameStart);
            std::string currertFilterArg;
            if (val_end != std::string::npos) {
                currertFilterArg = params.substr(nameEnd + 1, val_end - (nameEnd + 1));
            } else {
                currertFilterArg = params.substr(nameEnd + 1);
            }
            if (currertFilter == "clipbox") {
                if (currertFilterArg.length() == 2 * 4 * 4 * sizeof(float)) {
                    for (int row = 0; row < 4; ++row)
                        for (int col = 0; col < 4; ++col)
                            for (unsigned int car = 0; car < sizeof(osg::Matrixf::value_type); ++car) {
                                size_t idx = 2 * (sizeof(float) * (4 * row + col) + car);
                                char n1 = currertFilterArg[idx];
                                char n2 = currertFilterArg[idx+1];
                                n1 = (n1 <= '9') ? n1 - '0' : (n1 <= 'A') ? n1 - 'A' + 10 : n1 - 'a' + 10;
                                n2 = (n2 <= '9') ? n2 - '0' : (n2 <= 'A') ? n2 - 'A' + 10 : n2 - 'a' + 10;
                                unsigned char val = (n1 << 4) | n2;
                                ((unsigned char*)(&clipboxMat(row, col)))[car] = val;
                            }
                    filters.push_back(currertFilter);
                    filterVal.push_back(1.0f);
                } else {
                    OSG_WARN << EXTENSION_NAME " pseudo-loader clipbox filter argument needs exactly " << 2 * 4 * 3 * sizeof(float) << " bytes, not " << currertFilterArg.length() << " in string " << currertFilterArg << std::endl;
                }
            } else if (currertFilter == "compact") {
                makeCompactArrays = true;
            } else {
                float value;
                if (1 == sscanf(currertFilterArg.c_str(), "%f", &value)) {
                        filters.push_back(currertFilter);
                        filterVal.push_back(value);
                } else {
                    OSG_WARN << EXTENSION_NAME " pseudo-loader problem with argument for filter " << currertFilter << " can't parse "<< currertFilterArg << "as float." << std::endl;
                }
            }
            if (val_end != std::string::npos) {//found ',' after value
                nameStart = val_end + 1;
                nameEnd = params.find_first_of(',', nameStart);
            } else {
                nameEnd = std::string::npos;
            }
        }
        if (subFileName.empty())
        {
            OSG_WARN << "Missing subfilename for " EXTENSION_NAME " pseudo-loader" << std::endl;
            return ReadResult::FILE_NOT_HANDLED;
        }

        OSG_INFO << " params = \"" << params << "\"" << std::endl;
        OSG_INFO << " subFileName = \"" << subFileName << "\"" << std::endl;

#if 1
        ReaderWriter::ReadResult rr = osgDB::Registry::instance()->readNode(subFileName, options);
#else
        // recursively load the subfile.
        osgDB::ReaderWriter::Options* fc_opt;
        if (options) fc_opt = new osgDB::ReaderWriter::Options(*options);
        else if (osgDB::Registry::instance()->getOptions()) fc_opt = new osgDB::ReaderWriter::Options(*osgDB::Registry::instance()->getOptions());
        else fc_opt = new osgDB::ReaderWriter::Options();
        fc_opt->setObjectCacheHint(osgDB::Options::CACHE_ALL);
        std::string optstr = fc_opt->getOptionString();
        size_t start = 0;
        size_t space = optstr.find_first_of(' ', start);
        std::string option = optstr.substr(0, space);
        while (!option.empty()) {
            if (option.substr(0,6) == "clipbox")
                do something
        }
        ReaderWriter::ReadResult rr = osgDB::Registry::instance()->readNode(subFileName, fc_opt);
#endif
#if 0
        OSG_WARN << EXTENSION_NAME " options " << params << std::endl;
        if (rr.status() != osgDB::ReaderWriter::ReadResult::FILE_LOADED_FROM_CACHE) {
            OSG_WARN << EXTENSION_NAME " pseudo-loader expected file " << subFileName << " in cache!" << std::endl;
            //osgDB::Registry::instance()->addEntryToObjectCache(subFileName, rr.getObject(), 0.0);
            
        } else {
            if (osgDB::getFileExtension(subFileName) != ".osg")
                OSG_WARN << EXTENSION_NAME " pseudo-loader found    file " << subFileName << " in cache!" << std::endl;
        }
#endif
        osg::ref_ptr<osg::Node> result = rr.getNode();
        //if (rr.validNode()) return osg::ref_ptr<osg::Node>(rr.getNode());
		//osg::ref_ptr<osg::Node> result = osgDB::readRefNodeFile(subFileName, fc_opt);
		if (result == NULL) 
            return ReadResult::ERROR_IN_READING_FILE;
        //if (filters.empty() && !makeCompactArrays) return result;
        osg::CopyOp cpOpt = osg::CopyOp::DEEP_COPY_NODES | osg::CopyOp::DEEP_COPY_DRAWABLES | osg::CopyOp::DEEP_COPY_PRIMITIVES;
#if DBG_WRITE       
        makeCompactArrays = true;//DEBUG
#endif
        if (makeCompactArrays) { cpOpt.setCopyFlags(cpOpt.getCopyFlags() | osg::CopyOp::DEEP_COPY_ARRAYS); }
        //osg::ref_ptr<osg::Node> filtered = osg::clone(result.get(), osg::CopyOp::DEEP_COPY_NODES | osg::CopyOp::DEEP_COPY_DRAWABLES | osg::CopyOp::DEEP_COPY_PRIMITIVES | osg::CopyOp::DEEP_COPY_ARRAYS);
        //not compacting arrays: shallow copy primitives and arrays
        osg::ref_ptr<osg::Node> filtered = osg::clone(result.get(), cpOpt );

        filtered->setUserDataContainer(NULL);//don't share the container, or result->userdata will point to result!
        filtered->setUserData(result);//hook a copy of your original to keep it in cache!

        // find stateset to read clipbox and write minmax values
#if 0
        osg::ref_ptr<osg::StateSet> targetStateSet;
        if ((options != NULL) && options->getParentGroup().valid()) {
            targetStateSet = options->getParentGroup()->getOrCreateStateSet();
        }
        if (!targetStateSet.valid()) targetStateSet = new osg::StateSet();//dummy; will be destroyed
        //
        pointsFilterVisitor pv(filters, filterVal, targetStateSet.get(), clipboxMat);
#else
        //just add min/max to my own stateset; because this might be cached.
        osg::StateSet* org = filtered->getStateSet();
        if (org) {
            osg::StateSet* shalowCopy = osg::clone(org, osg::CopyOp::DEEP_COPY_UNIFORMS);
            filtered->setStateSet(shalowCopy);
        }
        pointsFilterVisitor pv(filters, filterVal, filtered->getOrCreateStateSet(), clipboxMat);
#endif
        if (makeCompactArrays) { pv.compactArrays = true; }
        filtered->accept(pv);
#if DBG_WRITE
        osg::Group *fg = filtered->asGroup();
        osg::Node* data = fg->getChild(0);
        osgDB::writeNodeFile(*data, "debug.osg");//DEBUG
#endif
		return filtered;
	}
};


// Add ourself to the Registry to instantiate the reader/writer.
REGISTER_OSGPLUGIN(EXTENSION_NAME, RW_NAME)

/*EOF*/

