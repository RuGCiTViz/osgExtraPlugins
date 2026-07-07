# osgExtraPlugins
some helper (pseudo-) fileformat plugins for OpenSceneGraph
# Building:
generally cmake should be able to configure the project if OpenSceneGraph 3.6.5 is found.
the configure.bat is tailored to my dev setup, and is not expected to work on your system.
# plugins:

`arg      provides arguments to .lua scripts`  
`child    selects a specific child of a loaded model (allows sharing of arrays and statesets)`  
`crn      compessed dds file format - see` [crunch/crnlib](https://github.com/binomialllc/crunch) ` `  
`dds      modified dds loader, responds to mmToSkip option creating a crude texturen LOD`  
`mip      pseudo loader controlling mmToSkip option`  
`ptfilter point cloud box filter`  
`qcrop    pseudo loader selectin half or quarter of an image`  
`wtms     pseudo loader for web tile map server`  
  
`brotli   brotli compressor plugin`  
`cfcells  cyclomedia cfcells (2015)`  
`Inventor slightly modified iv plugin (coin linkage)`  
`normalflip pseudo-loader: invert normals`  
`stdin    allow reading files from stdin`  
  
#plugins2
`kml      very crude unfinished plugin`

#Examples:
```
osgviewer "https://service.pdok.nl/hwh/luchtfotorgb/wmts/v1_0?layer=2025_ortho25&style=default&tilematrixset=EPSG%3A28992&Service=WMTS&Request=GetTile&Version=1.0.0&Format=image%2Fpng&TileMatrix=02&TileCol=2&TileRow=1.png.15.wmts.90,0,0.rot" --window 40 40 800 600
```
```
osgviewer --image dog_left_eye.jpg.lb.qcrop
```

