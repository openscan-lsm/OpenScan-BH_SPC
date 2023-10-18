#pragma once

#include <rapidjson/document.h>
#include <rapidjson/filereadstream.h>
#include <rapidjson/filewritestream.h>
#include <rapidjson/prettywriter.h>

#include <bitset>
#include <cstdio>
#include <string>

namespace rj = rapidjson;

class MetadataJsonWriter final {
    std::string const filename;
    rj::Document doc;

  public:
    MetadataJsonWriter(std::string const &filename) : filename(filename) {
        doc.SetObject();
        doc.AddMember("version", 0, doc.GetAllocator());
        doc.AddMember(
            "comment",
            "This file contains Becker-Hickl TCSPC-FLIM acquisition settings for the corresponding .spc file",
            doc.GetAllocator());
    }

    void Save() {
        FILE *fp = fopen(filename.c_str(), "wb");
        if (fp) {
            char buf[65535];
            rj::FileWriteStream stream(fp, buf, sizeof(buf));
            rj::PrettyWriter<rj::FileWriteStream> writer(stream);
            doc.Accept(writer);
            fclose(fp);
        }
    }

    void SetChannelMask(std::bitset<16> const &mask) {
        rj::Value array(rj::kArrayType);
        auto &allocator = doc.GetAllocator();
        for (size_t i = 0; i < mask.size(); ++i) {
            array.PushBack(mask[i], allocator);
        }
        doc.AddMember("enabled_channels", array, doc.GetAllocator());
    }

    void SetImageSize(uint32_t width, uint32_t height) {
        doc.AddMember("raster_width", width, doc.GetAllocator());
        doc.AddMember("raster_height", height, doc.GetAllocator());
    }

    void SetPixelRateHz(double pixelRateHz) {
        doc.AddMember("pixel_rate_hz", pixelRateHz, doc.GetAllocator());
    }

    void SetMacrotimeUnitsTenthNs(int macroTimeUnitsTenthNs) {
        doc.AddMember("macrotime_units_1_10_ns", macroTimeUnitsTenthNs,
                      doc.GetAllocator());
    }

    void SetLineDelayAndTime(int32_t delay, uint32_t time) {
        doc.AddMember("line_delay_macrotime_units", delay, doc.GetAllocator());
        doc.AddMember("line_time_macrotime_units", time, doc.GetAllocator());
    }

    void SetMarkerSettings(bool usePixelClock, uint32_t nMarkerBits,
                           uint32_t pixelMarkerBit, uint32_t lineMarkerBit,
                           uint32_t frameMarkerBit) {
        if (usePixelClock)
            doc.AddMember("use_pixel_clock", usePixelClock,
                          doc.GetAllocator());
        if (pixelMarkerBit < nMarkerBits)
            doc.AddMember("pixel_marker_bit", pixelMarkerBit,
                          doc.GetAllocator());
        if (lineMarkerBit < nMarkerBits)
            doc.AddMember("line_marker_bit", lineMarkerBit,
                          doc.GetAllocator());
        if (frameMarkerBit < nMarkerBits)
            doc.AddMember("frame_marker_bit", frameMarkerBit,
                          doc.GetAllocator());
    }
};

class MetadataJsonReader final {
    rj::Document doc;

  public:
    MetadataJsonReader(std::string const &filename) {
        FILE *fp = fopen(filename.c_str(), "rb");
        if (fp) {
            char buf[65535];
            rj::FileReadStream stream(fp, buf, sizeof(buf));
            doc.ParseStream(stream);
            fclose(fp);
        }
    }

    bool IsValid() const noexcept { return doc.IsObject(); }

    std::bitset<16> GetChannelMask() const {
        std::bitset<16> ret;
        if (!doc.HasMember("enabled_channels"))
            throw std::runtime_error("JSON field missing: enabled_channels");
        auto &array = doc["enabled_channels"];
        if (!array.IsArray())
            throw std::runtime_error("JSON enabled_channels must be array");
        if (array.GetArray().Size() != 16)
            throw std::runtime_error(
                "JSON enabled_channels array must have length 16");
        for (rj::SizeType i = 0; i < 16; ++i) {
            if (!array[i].IsBool())
                throw std::runtime_error(
                    "JSON enabled_channels array must contain bool elements");
            ret[i] = array[i].GetBool();
        }
        return ret;
    }

    uint32_t GetRasterWidth() const {
        if (!doc.HasMember("raster_width"))
            throw std::runtime_error("JSON field missing: raster_width");
        auto &width = doc["raster_width"];
        if (!width.IsUint())
            throw std::runtime_error("JSON raster_width must be integer");
        return width.GetUint();
    }

    uint32_t GetRasterHeight() const {
        if (!doc.HasMember("raster_height"))
            throw std::runtime_error("JSON field missing: raster_height");
        auto &height = doc["raster_height"];
        if (!height.IsUint())
            throw std::runtime_error(
                "JSON raster_height field must be integer");
        return height.GetUint();
    }

    double GetPixelRateHz() const {
        if (!doc.HasMember("pixel_rate_hz"))
            throw std::runtime_error("JSON field missing: pixel_rate_hz");
        auto &rate = doc["pixel_rate_hz"];
        if (!rate.IsDouble())
            throw std::runtime_error(
                "JSON pixel_rate_hz field must be real number");
        return rate.GetDouble();
    }

    int32_t GetLineDelay() const {
        if (!doc.HasMember("line_delay_macrotime_units"))
            return 0;
        auto &delay = doc["line_delay_macrotime_units"];
        if (!delay.IsInt())
            return 0;
        return delay.GetInt();
    }

    uint32_t GetLineTime() const {
        if (!doc.HasMember("line_time_macrotime_units"))
            throw std::runtime_error(
                "JSON field missing: line_time_macrotime_units");
        auto &time = doc["line_time_macrotime_units"];
        if (!time.IsUint())
            throw std::runtime_error(
                "JSON line_time_macrotime_units field must be integer");
        return time.GetUint();
    }

    bool GetUsePixelClock() const {
        if (!doc.HasMember("use_pixel_clock"))
            return false;
        auto &flag = doc["use_pixel_clock"];
        if (!flag.IsBool())
            return false;
        return flag.GetBool();
    }

    uint32_t GetLineMarkerBit() const {
        if (!doc.HasMember("line_marker_bit"))
            throw std::runtime_error("JSON field missting: line_marker_bit");
        auto &bit = doc["line_marker_bit"];
        if (!bit.IsUint())
            throw std::runtime_error(
                "JSON line_marker_bit field must be integer");
        return bit.GetUint();
    }
};
