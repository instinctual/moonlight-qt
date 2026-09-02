#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

class SdlInputHandler;

enum class PlankCursorPresenterOwner : std::uint16_t
{
    LocalMouse = 1,
    HostTablet = 2,
    LocalUi = 3,
};

struct PlankCursorPresenterImage
{
    std::uint64_t generation = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t hotspotX = 0;
    std::uint32_t hotspotY = 0;
    bool visible = false;
    std::vector<std::uint8_t> pixels;
};

struct PlankCursorPresenterPosition
{
    PlankCursorPresenterOwner owner = PlankCursorPresenterOwner::LocalMouse;
    std::uint64_t sequence = 0;
    std::int32_t clientX = 0;
    std::int32_t clientY = 0;
    std::uint32_t clientWidth = 0;
    std::uint32_t clientHeight = 0;
};

// Narrow retained-client boundary consumed by the PLANK2 cursor presenter
// sink. Keeping this independent of SdlInputHandler lets the adapter contract
// be exercised without constructing the full retained streaming stack.
class IPlankCursorPresenterTarget
{
public:
    virtual ~IPlankCursorPresenterTarget() = default;

    virtual bool available() const = 0;
    virtual bool presentImage(const PlankCursorPresenterImage& image) = 0;
    virtual bool presentPosition(const PlankCursorPresenterPosition& position) = 0;
    virtual bool reset() = 0;
};

// A lifetime-safe, main-thread-affine target for the retained SDL3/Wayland
// cursor implementation. Presenter streams may outlive SdlInputHandler, but
// detach() prevents them from retaining or calling a destroyed handler.
class PlankCursorPresenterTarget final : public IPlankCursorPresenterTarget
{
public:
    explicit PlankCursorPresenterTarget(SdlInputHandler* handler);

    bool available() const override;
    bool presentImage(const PlankCursorPresenterImage& image) override;
    bool presentPosition(const PlankCursorPresenterPosition& position) override;
    bool reset() override;
    void detach();

private:
    mutable std::mutex m_Mutex;
    SdlInputHandler* m_Handler;
};
