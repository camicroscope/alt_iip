/*  IIP Server: OpenSlide whole-slide image handler

    Copyright (C) 2009-2025 IIPImage.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "OpenSlideImage.h"
#include "Logger.h"
#include <cmath>
#include <sstream>

using namespace std;

extern Logger logfile;

#define TILESIZE 256


void OpenSlideImage::openImage()
{
  string filename = getFileName( currentX, currentY );
  updateTimestamp( filename );

  // Close any previously open handle
  closeImage();

  osr = openslide_open( filename.c_str() );
  if( !osr ){
    throw file_error( "OpenSlide :: Unable to open '" + filename + "'" );
  }
  const char* err = openslide_get_error( osr );
  if( err ){
    throw file_error( "OpenSlide :: Error opening '" + filename + "': " + string(err) );
  }

  // Load metadata if not already done
  if( bpc == 0 ) loadImageInfo( currentX, currentY );

  isSet = true;
}


void OpenSlideImage::loadImageInfo( int seq, int ang )
{
  if( !osr ) return;

  currentX = seq;
  currentY = ang;

  image_widths.clear();
  image_heights.clear();
  openslide_level_for_res.clear();
  extra_factor_for_res.clear();

  // Get full-resolution (level 0) dimensions
  int64_t w0, h0;
  openslide_get_level0_dimensions( osr, &w0, &h0 );

  // --- Build virtual power-of-2 pyramid ---
  // image_widths[0] = largest (full resolution), matching IIPImage convention.
  // We halve until both dimensions fit within a single tile.

  unsigned int w = (unsigned int)w0;
  unsigned int h = (unsigned int)h0;

  image_widths.push_back( w );
  image_heights.push_back( h );
  openslide_level_for_res.push_back( 0 );
  extra_factor_for_res.push_back( 1 );

  int os_level_count = openslide_get_level_count( osr );

  while( (w > TILESIZE) || (h > TILESIZE) ){

    w = (unsigned int)ceil( w / 2.0 );
    h = (unsigned int)ceil( h / 2.0 );

    // Downsample factor of this virtual level relative to level 0
    double virt_ds = (double)w0 / (double)w;

    // Find the best (highest-resolution) native OpenSlide level whose downsample
    // is <= our virtual level's downsample (i.e. at least as detailed as needed).
    // OpenSlide levels are ordered 0=largest → N=smallest.
    int best_os_level = 0;
    for( int i = 0; i < os_level_count; i++ ){
      double ds = openslide_get_level_downsample( osr, i );
      if( ds <= virt_ds + 0.001 ){
        best_os_level = i;
      }
    }

    double best_ds = openslide_get_level_downsample( osr, best_os_level );
    int extra = (int)round( virt_ds / best_ds );
    if( extra < 1 ) extra = 1;

    image_widths.push_back( w );
    image_heights.push_back( h );
    openslide_level_for_res.push_back( best_os_level );
    extra_factor_for_res.push_back( extra );
  }

  numResolutions = (unsigned int)image_widths.size();

  // All levels use the same tile size
  tile_widths.assign( numResolutions, TILESIZE );
  tile_heights.assign( numResolutions, TILESIZE );

  // OpenSlide always outputs 8-bit RGBA converted to 3-channel RGB
  channels   = 3;
  bpc        = 8;
  colorspace = ColorSpace::sRGB;

  min.assign( channels, 0.0f );
  max.assign( channels, 255.0f );

  // Store all OpenSlide properties in the metadata map
  const char* const* prop_names = openslide_get_property_names( osr );
  if( prop_names ){
    for( int i = 0; prop_names[i]; i++ ){
      const char* val = openslide_get_property_value( osr, prop_names[i] );
      if( val ) metadata.insert( {string(prop_names[i]), string(val)} );
    }
  }

  if( IIPImage::logging ){
    logfile << "OpenSlide :: " << numResolutions << " resolution levels" << endl
            << "OpenSlide :: Full resolution: " << w0 << "x" << h0 << endl
            << "OpenSlide :: Native OpenSlide levels: " << os_level_count << endl;
  }

  isSet = true;
}


void OpenSlideImage::closeImage()
{
  if( osr ){
    openslide_close( osr );
    osr = NULL;
  }
}


RawTile OpenSlideImage::getTile( int seq, int ang, unsigned int res, int layers,
                                  unsigned int tile, ImageEncoding e )
{
  if( res >= numResolutions ){
    ostringstream oss;
    oss << "OpenSlide :: Asked for non-existent resolution: " << res;
    throw file_error( oss.str() );
  }

  // Convert IIP resolution (0=smallest) to array index (0=largest)
  unsigned int vi = (unsigned int)getNativeResolution( (int)res );

  unsigned int imgW = image_widths[vi];
  unsigned int imgH = image_heights[vi];
  unsigned int tw   = tile_widths[0];
  unsigned int th   = tile_heights[0];

  // Tile grid
  unsigned int ntlx = (imgW + tw - 1) / tw;
  unsigned int ntly = (imgH + th - 1) / th;

  if( tile >= ntlx * ntly ){
    ostringstream oss;
    oss << "OpenSlide :: Asked for non-existent tile: " << tile;
    throw file_error( oss.str() );
  }

  unsigned int tx   = tile % ntlx;
  unsigned int ty   = tile / ntlx;
  unsigned int xoff = tx * tw;
  unsigned int yoff = ty * th;

  // Clamp edge tiles
  if( xoff + tw > imgW ) tw = imgW - xoff;
  if( yoff + th > imgH ) th = imgH - yoff;

  RawTile rawtile( tile, res, seq, ang, tw, th, channels, bpc );
  rawtile.filename  = getImagePath();
  rawtile.timestamp = timestamp;
  rawtile.allocate();

  process( res, (int)xoff, (int)yoff, tw, th, rawtile.data );

  return rawtile;
}


RawTile OpenSlideImage::getRegion( int ha, int va, unsigned int res, int layers,
                                    int x, int y, unsigned int w, unsigned int h )
{
  RawTile rawtile( 0, res, ha, va, w, h, channels, bpc );
  rawtile.filename  = getImagePath();
  rawtile.timestamp = timestamp;
  rawtile.allocate();

  process( res, x, y, w, h, rawtile.data );

  return rawtile;
}


void OpenSlideImage::process( unsigned int res, int x, int y,
                               unsigned int tw, unsigned int th, void* d )
{
  // Convert IIP resolution to pyramid array index (0 = largest/full resolution)
  unsigned int vi = (unsigned int)getNativeResolution( (int)res );

  int    os_level = openslide_level_for_res[vi];
  int    extra    = extra_factor_for_res[vi];

  // Convert virtual-level pixel coordinates to level-0 coordinates.
  // image_widths[0] is the level-0 width; image_widths[vi] is the virtual level width.
  double virt_ds = (double)image_widths[0] / (double)image_widths[vi];
  int64_t x0 = (int64_t)( x * virt_ds + 0.5 );
  int64_t y0 = (int64_t)( y * virt_ds + 0.5 );

  // Read extra*tw x extra*th pixels from the chosen native OpenSlide level,
  // then box-filter down to tw*th if extra > 1.
  unsigned int read_w = tw * (unsigned int)extra;
  unsigned int read_h = th * (unsigned int)extra;

  vector<uint32_t> buf( read_w * read_h );
  openslide_read_region( osr, buf.data(), x0, y0, os_level,
                         (int64_t)read_w, (int64_t)read_h );

  const char* err = openslide_get_error( osr );
  if( err ){
    throw file_error( "OpenSlide :: Error reading region: " + string(err) );
  }

  unsigned char* dest = (unsigned char*)d;

  if( extra == 1 ){
    // Direct BGRA to RGB conversion (OpenSlide pixels are 0xAARRGGBB)
    for( unsigned int i = 0; i < tw * th; i++ ){
      uint32_t p = buf[i];
      dest[i*3 + 0] = (unsigned char)((p >> 16) & 0xFF);  // R
      dest[i*3 + 1] = (unsigned char)((p >>  8) & 0xFF);  // G
      dest[i*3 + 2] = (unsigned char)( p        & 0xFF);  // B
    }
  }
  else{
    // Box-filter extra*extra pixels to 1 output pixel, then BGRA to RGB
    for( unsigned int j = 0; j < th; j++ ){
      for( unsigned int i = 0; i < tw; i++ ){
        unsigned int r = 0, g = 0, b = 0;
        for( int dj = 0; dj < extra; dj++ ){
          for( int di = 0; di < extra; di++ ){
            uint32_t p = buf[ (j*(unsigned int)extra + (unsigned int)dj) * read_w
                              + i*(unsigned int)extra + (unsigned int)di ];
            r += (p >> 16) & 0xFF;
            g += (p >>  8) & 0xFF;
            b +=  p        & 0xFF;
          }
        }
        unsigned int n = (unsigned int)(extra * extra);
        dest[ (j*tw + i)*3 + 0 ] = (unsigned char)(r / n);
        dest[ (j*tw + i)*3 + 1 ] = (unsigned char)(g / n);
        dest[ (j*tw + i)*3 + 2 ] = (unsigned char)(b / n);
      }
    }
  }
}
