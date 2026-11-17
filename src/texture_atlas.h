#ifndef TEXTURE_ATLAS_H
#define TEXTURE_ATLAS_H

#include <cassert>
#include <iostream>
#include <limits>
#include <vector>
#include <utility>
#include <algorithm>
#include <stdexcept>

#include "vector_util.h"
#include <SFML/Graphics.hpp>

// RectPacker maintains two offsets into an overall bin size:
//  - A line y-offset, representing the current horizontal line on which to place new rectangles
//      horizontally along (the line corresponding to the rectangle tops)
//  - An x-offset into the line, representing the x-coordinate to align the next rectangle left-side
// Furthermore it maintains the maximum texture height encountered along the current line.
// The RectPacker places rectangles left to right along this line until it cannot fit the next
//  rectangle. Once this happens, the line is shifted down to meet the bottom of the tallest texture
//  placed on the line, and the x-offset is reset. This continues until the next rectangle
//  packed cannot be fit on the line within the bin space.
class RectPacker {
public:
    RectPacker(const sf::Vector2f& size) : mSize{size} {};

    // pack returns { succeeded, rect id }. If not succeeded, id is irrelevant (but ideally should
    // be set to the maximum possible value).
    std::pair<bool, size_t> pack(const sf::Vector2f& rectSize) {
        // Check if the rectangle is wider than the entire bin.
        if (rectSize.x > mSize.x) {
            return {false, std::numeric_limits<size_t>::max()};
        }

        // Check if the rectangle fits horizontally in the current line.
        if (mXOffset + rectSize.x > mSize.x) {
            // Move down to the bottom of the tallest rectangle.
            mYOffset += mLineHeight;

            // Start a new line.
            mXOffset = 0.f;
            mLineHeight = 0.f;
        }

        // Check if the rectangle fits vertically.
        if (mYOffset + rectSize.y > mSize.y) {
            return {false, std::numeric_limits<size_t>::max()};
        }

        // Store the rectangle's bounds.
        size_t rectID = mRects.size();

        mRects.emplace_back(sf::Vector2f{mXOffset, mYOffset}, rectSize);

        // Advance the horizontal offset.
        mXOffset += rectSize.x;

        // Update the maximum height of the current line.
        mLineHeight = std::max(mLineHeight, rectSize.y);

        return {true, rectID}; 
    }

    // Returns the coordinates and size of a previously returned rectangle id.
    // Note that in this coordinate system, y points downwards from the origin at the top-left.
    sf::FloatRect getBounds(size_t rectID) const { 
        assert(rectID < mRects.size());
        return mRects[rectID];
    }

private:
    sf::Vector2f mSize;
    float mXOffset = 0.f;
    float mYOffset = 0.f;
    float mLineHeight = 0.f;

    std::vector<sf::FloatRect> mRects;
};

// Returns a new image which pages the given img with *pixels* pixels on each side.
// The colour of new border pixels are taken by sampling the closest valid pixel in the original
// image.
//
// NOTE: a production implementation might also support other edge wrapping behaviors, like cycling
// around to the other side of the texture. See https://learnopengl.com/Getting-started/Textures
// Our implementation corresponds to GL_CLAMP_TO_EDGE
inline sf::Image padImage(const sf::Image& img, unsigned int pixels) { 
    // Get the original image dimensions.
    sf::Vector2u originalSize = img.getSize();

    // Calculate the dimensions of the padded image.
    sf::Vector2u paddedSize{originalSize.x + 2 * pixels, originalSize.y + 2 * pixels};

    // Create the new image.
    sf::Image padded(paddedSize);

    // Iterate through every pixel in the padded image.
    for (unsigned int y = 0; y < paddedSize.y; y++) {
        for (unsigned int x = 0; x < paddedSize.x; x++) {

            // Find the corresponding pixel in the original image.
            // ternary operators evaluate to 0 if true, else std:min
            unsigned int srcX = (x < pixels) ? 0 : std::min(x - pixels, originalSize.x - 1);
            unsigned int srcY = (y < pixels) ? 0 : std::min(y - pixels, originalSize.y - 1);

            // Copy the closest valid pixel.
            padded.setPixel({x, y}, img.getPixel({srcX, srcY}));
        }
    }

    return padded;
}
using TextureID = size_t;

class TextureAtlas {
public:
    TextureAtlas(sf::Vector2u binSize, size_t padding)
        : mTexture(binSize), mPacker(sf::Vector2f(mTexture.getSize())), mPadding{padding} {}

    TextureID registerTexture(const std::string& filepath) {
        // Load the image from the filepath.
        sf::Image img;
        if (!img.loadFromFile(filepath)) {
            throw std::runtime_error("Failed to load texture: " + filepath);
        }

        // Add padding around the image.
        sf::Image padded = padImage(img, static_cast<unsigned int>(mPadding));

        // Pack the padded image into the atlas.
        sf::Vector2f paddedSize(padded.getSize());
        auto [success, rectID] = mPacker.pack(paddedSize);

        if (!success) {
            throw std::runtime_error("Texture atlas is full!");
        }

        // Get the position assigned by the rectangle packer.
        sf::FloatRect bounds = mPacker.getBounds(rectID);

        // Copy the padded image into the atlas texture.
        mTexture.update(padded, {static_cast<unsigned int>(bounds.position.x), static_cast<unsigned int>(bounds.position.y)});

        // Return the rectangle ID as the texture ID.
        return rectID;
    }

    sf::FloatRect getBoundsUV(TextureID id) const { 
        // Get the packed rectangle.
        sf::FloatRect bounds = mPacker.getBounds(id);

        // Get the atlas dimensions.
        sf::Vector2f atlasSize(mTexture.getSize());

        // Remove the padding from the rectangle.
        float x = bounds.position.x + static_cast<float>(mPadding);
        float y = bounds.position.y + static_cast<float>(mPadding);

        float width = bounds.size.x - 2.f * static_cast<float>(mPadding);
        float height = bounds.size.y - 2.f * static_cast<float>(mPadding);

        // Convert pixel coordinates into OpenGL UV coordinates.
        float u = x / atlasSize.x;
        float v = 1.f - (y + height) / atlasSize.y;

        float uWidth = width / atlasSize.x;
        float vHeight = height / atlasSize.y;

        return sf::FloatRect({u, v}, {uWidth, vHeight});
    }

    sf::Vector2u getSizePixels(TextureID id) const {     
        // Get the packed rectangle.
        sf::FloatRect bounds = mPacker.getBounds(id);

        // Remove the padding from the dimensions.
        float width = bounds.size.x - 2.f * static_cast<float>(mPadding);
        float height = bounds.size.y - 2.f * static_cast<float>(mPadding);

        return {static_cast<unsigned int>(width), static_cast<unsigned int>(height)};
    }

    const sf::Texture& getTexture() const { return mTexture; }

private:
    sf::Texture mTexture;
    RectPacker mPacker;
    size_t mPadding;
};

#endif  // TEXTURE_ATLAS_H
