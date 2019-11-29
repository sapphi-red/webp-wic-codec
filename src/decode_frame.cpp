// Copyright 2010 Google Inc.
//
// This code is licensed under the same terms as WebM:
//  Software License Agreement:  http://www.webmproject.org/license/software/
//  Additional IP Rights Grant:  http://www.webmproject.org/license/additional/
// -----------------------------------------------------------------------------
//
// Interface to the VP8 frame.
//
// Author: Mikolaj Zalewski (mikolajz@google.com)

#include <windows.h>
#include "decode_frame.h"

#include <cstdlib>
#include <memory>

#include "src/webp/decode.h"

#ifdef WEBP_DEBUG_LOGGING
#include "stopwatch.h"
#endif  // WEBP_DEBUG_LOGGING

const int kBytesPerPixel = 4;

RGBImage::~RGBImage() {
  std::free(rgb);
}

HRESULT DecodeFrame::CreateFromVP8Stream(BYTE* vp8_bitstream, DWORD stream_size, ComPtr<DecodeFrame>* ppOutput) {
  TRACE1("stream_size=%d\n", stream_size);
  HRESULT result = S_OK;
  ComPtr<DecodeFrame> output;

  (*ppOutput).reset(NULL);

  std::auto_ptr<RGBImage> pImage(new (std::nothrow) RGBImage());
  if (pImage.get() == NULL)
    return E_OUTOFMEMORY;

#ifdef WEBP_DEBUG_LOGGING
  Stopwatch stopwatch;
  StopwatchReadAndReset(&stopwatch);
#endif  // WEBP_DEBUG_LOGGING

  // Note: according to the documentation, the actual decoding should happen
  // in CopyPixels. However, this would need to be implemented efficiently, as
  // e.g., the photo viewers load the image row by row, with multiple calls to
  // CopyPixels.
  // TODO: Do the decoding on the first call to CopyPixels to be OK with the
  // letter of the documentation. Maybe we could do progressive decoding if the
  // callers request the image in the top to bottom order?
  pImage->rgb = WebPDecodeBGRA(vp8_bitstream, stream_size,
                               &pImage->width, &pImage->height);
  pImage->stride = pImage->width * kBytesPerPixel;
  const bool decodedImage = (pImage->rgb != NULL);

#ifdef WEBP_DEBUG_LOGGING
  double time = StopwatchReadAndReset(&stopwatch);
  TRACE1("Decode (VP8 -> BGRA) time: %f\n", time);
#endif  // WEBP_DEBUG_LOGGING

  if (!decodedImage) {
    // We don't know what was the problem, but we assume it's a problem with
    // the content.
    // TODO: get better error codes.
    // Note: Win7 jpeg codec seems to prefer to return WINCODEC_ERR_BADHEADER
    // even for problems in the bitstream.
    TRACE("Couldn't decode VP8 stream.\n");
    return WINCODEC_ERR_BADIMAGE;
  }

  // Sanity checks:
  if (pImage->width <= 0 || pImage->height <= 0) {
    TRACE("Invalid sizes from decoder!\n");
    return E_FAIL;
  }

  output.reset(new (std::nothrow) DecodeFrame(pImage.release()));
  if (output.get() == NULL)
    return E_OUTOFMEMORY;

  (*ppOutput).reset(output.new_ref());
  return S_OK;
}

HRESULT DecodeFrame::QueryInterface(REFIID riid, void **ppvObject) {
  TRACE2("(%s, %p)\n", debugstr_guid(riid), ppvObject);

  if (ppvObject == NULL)
    return E_INVALIDARG;
  *ppvObject = NULL;

  if (!IsEqualGUID(riid, IID_IUnknown) &&
      !IsEqualGUID(riid, IID_IWICBitmapFrameDecode) &&
      !IsEqualGUID(riid, IID_IWICBitmapSource))
    return E_NOINTERFACE;
  this->AddRef();
  *ppvObject = static_cast<IWICBitmapFrameDecode*>(this);
  return S_OK;
}

HRESULT DecodeFrame::GetSize(UINT *puiWidth, UINT *puiHeight) {
  TRACE2("(%p, %p)\n", puiWidth, puiHeight);
  if (puiWidth == NULL || puiHeight == NULL)
    return E_INVALIDARG;
  *puiWidth = (UINT)image_->width;
  *puiHeight = (UINT)image_->height;
  TRACE2("ret: %u x %u\n", *puiWidth, *puiHeight);
  return S_OK;
}

HRESULT DecodeFrame::GetPixelFormat(WICPixelFormatGUID *pPixelFormat) {
  TRACE1("(%p)\n", pPixelFormat);
  if (pPixelFormat == NULL)
    return E_INVALIDARG;
  *pPixelFormat = GUID_WICPixelFormat32bppBGRA;
  return S_OK;
}

HRESULT DecodeFrame::GetResolution(double *pDpiX, double *pDpiY) {
  TRACE2("(%p, %p)\n", pDpiX, pDpiY);
  // Let's assume square pixels. 96dpi seems to be a reasonable default.
  *pDpiX = 96;
  *pDpiY = 96;
  return S_OK;
}

HRESULT DecodeFrame::CopyPalette(IWICPalette *pIPalette) {
  TRACE1("(%p)\n", pIPalette);
  return WINCODEC_ERR_PALETTEUNAVAILABLE;
}

HRESULT DecodeFrame::CopyPixels(const WICRect *prc, UINT cbStride, UINT cbBufferSize, BYTE *pbBuffer) {
  TRACE4("(%p, %u, %u, %p)\n", prc, cbStride, cbBufferSize, pbBuffer);
  if (pbBuffer == NULL)
    return E_INVALIDARG;
  WICRect rect = {0, 0, image_->width, image_->height};
  if (prc)
    rect = *prc;

  if (rect.Width < 0 || rect.Height < 0 || rect.X < 0 || rect.Y < 0)
    return E_INVALIDARG;
  if (rect.X + rect.Width > image_->width ||
      rect.Y + rect.Height > image_->height)
    return E_INVALIDARG;

  // Divisions instead of multiplications to avoid integer overflows:
  if (cbStride / kBytesPerPixel < static_cast<UINT>(rect.Width))
    return E_INVALIDARG;
  if (cbBufferSize / cbStride < static_cast<UINT>(rect.Height))
    return WINCODEC_ERR_INSUFFICIENTBUFFER;

  if (rect.Width == 0 || rect.Height == 0)
    return S_OK;

  BYTE* dst_buffer = pbBuffer;
  const int src_stride = image_->stride;
  const int x_offset = rect.X * kBytesPerPixel;
  const int width = rect.Width * kBytesPerPixel;
  for (int src_y = rect.Y; src_y < rect.Y + rect.Height; ++src_y) {
    memcpy(dst_buffer, image_->rgb + src_y * src_stride + x_offset, width);
    dst_buffer += cbStride;
  }

  return S_OK;
}

HRESULT DecodeFrame::GetMetadataQueryReader(IWICMetadataQueryReader **ppIMetadataQueryReader) {
  TRACE1("(%p)\n", ppIMetadataQueryReader);
  return WINCODEC_ERR_UNSUPPORTEDOPERATION;
}

HRESULT DecodeFrame::GetColorContexts(UINT cCount, IWICColorContext **ppIColorContexts, UINT *pcActualCount) {
  TRACE3("(%d, %p, %p)\n", cCount, ppIColorContexts, pcActualCount);
  if (pcActualCount == NULL)
    return E_INVALIDARG;
  *pcActualCount = 0;
  return S_OK;
}

HRESULT DecodeFrame::GetThumbnail(IWICBitmapSource **ppIThumbnail) {
  TRACE1("(%p)\n", ppIThumbnail);
  return WINCODEC_ERR_CODECNOTHUMBNAIL;
}


HRESULT DummyFrame::QueryInterface(REFIID riid, void **ppvObject) {
  TRACE2("(%s, %p)\n", debugstr_guid(riid), ppvObject);

  if (ppvObject == NULL)
    return E_INVALIDARG;
  *ppvObject = NULL;

  if (!IsEqualGUID(riid, IID_IUnknown) && !IsEqualGUID(riid, IID_IWICBitmapFrameDecode))
    return E_NOINTERFACE;
  this->AddRef();
  *ppvObject = static_cast<IWICBitmapFrameDecode*>(this);
  return S_OK;
}
