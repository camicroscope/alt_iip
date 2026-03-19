#ifndef OPENSLIDEIMAGE_H
#define OPENSLIDEIMAGE_H

#include <openslide.h>
#include <string>
#include <vector>
#include "IIPImage.h"

/// Image class for OpenSlide-supported whole-slide images. Inherits from IIPImage.
class OpenSlideImage : public IIPImage {

 public:

  /// Constructor from path string
  /** @param p path to image file */
  OpenSlideImage( const std::string& p ) : IIPImage(p), osr(NULL) { isFile = true; };

  /// Constructor from IIPImage object (preserves isFile, prefix/suffix, timestamp, etc.)
  /** @param image IIPImage object */
  OpenSlideImage( const IIPImage& image ) : IIPImage(image), osr(NULL) {};

  /// Destructor
  ~OpenSlideImage() { closeImage(); };

  /// Overloaded function for opening an OpenSlide image
  void openImage();

  /// Overloaded function for loading image metadata and building the virtual pyramid
  /** @param seq horizontal sequence angle
      @param ang vertical sequence angle
   */
  void loadImageInfo( int seq, int ang );

  /// Overloaded function for closing an OpenSlide image
  void closeImage();

  /// Overloaded function for getting a particular tile
  /** @param seq horizontal sequence angle
      @param ang vertical sequence angle
      @param res resolution level
      @param layers number of quality layers (unused for OpenSlide)
      @param tile tile number
      @param e image encoding
   */
  RawTile getTile( int seq, int ang, unsigned int res, int layers,
                   unsigned int tile, ImageEncoding e = ImageEncoding::RAW );

  /// Overloaded function for returning a region for a given angle and resolution
  /** @param ha horizontal angle
      @param va vertical angle
      @param res resolution level
      @param layers number of quality layers (unused)
      @param x x coordinate
      @param y y coordinate
      @param w width of region
      @param h height of region
      @return RawTile containing the requested region
   */
  RawTile getRegion( int ha, int va, unsigned int res, int layers,
                     int x, int y, unsigned int w, unsigned int h );

 private:

  /// Main pixel processing function: reads from OpenSlide and converts BGRA→RGB
  /** @param res IIP resolution level
      @param x x coordinate at this resolution
      @param y y coordinate at this resolution
      @param tw tile/region width
      @param th tile/region height
      @param d output buffer (RGB, 8-bit)
   */
  void process( unsigned int res, int x, int y, unsigned int tw, unsigned int th, void* d );

  openslide_t *osr;  ///< OpenSlide handle

  /// For each virtual pyramid level (index 0 = largest), the best native OpenSlide level to use
  std::vector<int> openslide_level_for_res;

  /// For each virtual pyramid level, the extra integer downsampling factor applied after OpenSlide read
  std::vector<int> extra_factor_for_res;
};

#endif
