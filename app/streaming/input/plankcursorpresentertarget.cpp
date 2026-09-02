#include "streaming/input/plankcursorpresentertarget.h"

#include "streaming/input/input.h"

#include <SDL3/SDL.h>

PlankCursorPresenterTarget::PlankCursorPresenterTarget(
        SdlInputHandler* handler)
    : m_Handler(handler)
{
}

bool PlankCursorPresenterTarget::available() const
{
    if (!SDL_IsMainThread()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_Mutex);
    return m_Handler != nullptr &&
            m_Handler->isPlank2CursorPresenterAvailable();
}

bool PlankCursorPresenterTarget::presentImage(
        const PlankCursorPresenterImage& image)
{
    if (!SDL_IsMainThread()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_Handler == nullptr) {
        return false;
    }
    return m_Handler->applyPlank2CursorImage(image);
}

bool PlankCursorPresenterTarget::presentPosition(
        const PlankCursorPresenterPosition& position)
{
    if (!SDL_IsMainThread()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_Handler == nullptr) {
        return false;
    }
    return m_Handler->applyPlank2CursorPosition(position);
}

bool PlankCursorPresenterTarget::reset()
{
    if (!SDL_IsMainThread()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_Mutex);
    if (m_Handler == nullptr) {
        return false;
    }
    m_Handler->resetPlank2CursorPresenter();
    return true;
}

void PlankCursorPresenterTarget::detach()
{
    std::lock_guard<std::mutex> lock(m_Mutex);
    m_Handler = nullptr;
}
