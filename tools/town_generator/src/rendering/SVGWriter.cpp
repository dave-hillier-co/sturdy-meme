#include <town_generator/rendering/SVGWriter.h>
#include <fstream>
#include <cstdio>

namespace town_generator {

SVGWriter::SVGWriter(float width, float height, float minX, float minY, float maxX, float maxY)
    : width_(width), height_(height), minX_(minX), minY_(minY), maxX_(maxX), maxY_(maxY) {
}

std::string SVGWriter::indent() const {
    return std::string(indentLevel_ * 2, ' ');
}

std::string SVGWriter::colorToHex(uint32_t color) const {
    char buf[8];
    snprintf(buf, sizeof(buf), "#%06X", color & 0xFFFFFF);
    return std::string(buf);
}

std::string SVGWriter::pointsToPath(const Polygon& poly, bool closed) const {
    if (poly.empty()) return "";

    std::stringstream ss;
    ss << std::fixed << std::setprecision(2);

    ss << "M " << poly[0].x << " " << poly[0].y;
    for (size_t i = 1; i < poly.size(); i++) {
        ss << " L " << poly[i].x << " " << poly[i].y;
    }
    if (closed) {
        ss << " Z";
    }

    return ss.str();
}

void SVGWriter::drawPolygon(const Polygon& poly, uint32_t fill, uint32_t stroke, float strokeWidth) {
    if (poly.empty()) return;

    content_ << indent() << "<path d=\"" << pointsToPath(poly, true) << "\"";
    content_ << " fill=\"" << colorToHex(fill) << "\"";

    if (stroke != 0xFFFFFFFF && strokeWidth > 0) {
        content_ << " stroke=\"" << colorToHex(stroke) << "\"";
        content_ << " stroke-width=\"" << strokeWidth << "\"";
        content_ << " stroke-linejoin=\"miter\"";
    } else {
        content_ << " stroke=\"none\"";
    }

    content_ << "/>\n";
}

void SVGWriter::drawPolygonStrokeOnly(const Polygon& poly, uint32_t stroke, float strokeWidth) {
    if (poly.empty()) return;

    content_ << indent() << "<path d=\"" << pointsToPath(poly, true) << "\"";
    content_ << " fill=\"none\"";
    content_ << " stroke=\"" << colorToHex(stroke) << "\"";
    content_ << " stroke-width=\"" << strokeWidth << "\"";
    content_ << " stroke-linejoin=\"miter\"";
    content_ << "/>\n";
}

void SVGWriter::drawPolyline(const Polygon& poly, uint32_t stroke, float strokeWidth,
                              const std::string& lineCap) {
    if (poly.size() < 2) return;

    content_ << indent() << "<path d=\"" << pointsToPath(poly, false) << "\"";
    content_ << " fill=\"none\"";
    content_ << " stroke=\"" << colorToHex(stroke) << "\"";
    content_ << " stroke-width=\"" << strokeWidth << "\"";
    content_ << " stroke-linecap=\"" << lineCap << "\"";
    content_ << " stroke-linejoin=\"round\"";
    content_ << "/>\n";
}

void SVGWriter::drawCircle(float cx, float cy, float r, uint32_t fill, uint32_t stroke,
                            float strokeWidth) {
    content_ << indent() << "<circle cx=\"" << cx << "\" cy=\"" << cy << "\" r=\"" << r << "\"";
    content_ << " fill=\"" << colorToHex(fill) << "\"";

    if (stroke != 0xFFFFFFFF && strokeWidth > 0) {
        content_ << " stroke=\"" << colorToHex(stroke) << "\"";
        content_ << " stroke-width=\"" << strokeWidth << "\"";
    } else {
        content_ << " stroke=\"none\"";
    }

    content_ << "/>\n";
}

void SVGWriter::drawLine(float x1, float y1, float x2, float y2, uint32_t stroke,
                          float strokeWidth, const std::string& lineCap) {
    content_ << indent() << "<line x1=\"" << x1 << "\" y1=\"" << y1
             << "\" x2=\"" << x2 << "\" y2=\"" << y2 << "\"";
    content_ << " stroke=\"" << colorToHex(stroke) << "\"";
    content_ << " stroke-width=\"" << strokeWidth << "\"";
    content_ << " stroke-linecap=\"" << lineCap << "\"";
    content_ << "/>\n";
}

void SVGWriter::beginGroup(const std::string& id) {
    content_ << indent() << "<g";
    if (!id.empty()) {
        content_ << " id=\"" << id << "\"";
    }
    content_ << ">\n";
    indentLevel_++;
}

void SVGWriter::endGroup() {
    indentLevel_--;
    content_ << indent() << "</g>\n";
}

std::string SVGWriter::toString() const {
    std::stringstream ss;
    ss << std::fixed << std::setprecision(2);

    ss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    ss << "<svg xmlns=\"http://www.w3.org/2000/svg\" ";
    ss << "width=\"" << width_ << "\" height=\"" << height_ << "\" ";
    ss << "viewBox=\"" << minX_ << " " << minY_ << " "
       << (maxX_ - minX_) << " " << (maxY_ - minY_) << "\">\n";
    ss << content_.str();
    ss << "</svg>\n";

    return ss.str();
}

bool SVGWriter::saveToFile(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) {
        return false;
    }
    file << toString();
    return true;
}

} // namespace town_generator
