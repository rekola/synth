#include <cellui/gtk/GlibEventLoop.h>

#include <glib-unix.h>
#include <glib.h>

namespace cellui::gtk {

namespace {

// A GSource for one application source: ready when any of its descriptors
// is readable.
struct FdSource {
  GSource base;
  std::vector<gpointer> * tags;
  EventLoop::Handler * handler;
  bool * ran;
};

gboolean fdCheck(GSource * source) {
  auto * s = reinterpret_cast<FdSource *>(source);
  for (gpointer tag : *s->tags) {
    if (g_source_query_unix_fd(source, tag) & (G_IO_IN | G_IO_HUP | G_IO_ERR)) return TRUE;
  }
  return FALSE;
}

gboolean fdDispatch(GSource * source, GSourceFunc, gpointer) {
  auto * s = reinterpret_cast<FdSource *>(source);
  *s->ran = true;
  (*s->handler)();
  return G_SOURCE_CONTINUE;
}

void fdFinalize(GSource * source) {
  auto * s = reinterpret_cast<FdSource *>(source);
  delete s->tags;
  delete s->handler;
}

GSourceFuncs fd_funcs = { nullptr, fdCheck, fdDispatch, fdFinalize, nullptr, nullptr };

// Only there to end a wait after the turn's time limit, which prepare()
// reports as the poll timeout. (Setting a ready time every turn would signal
// the context's own wakeup descriptor each time, and the loop would spin.)
struct TimerSource {
  GSource base;
  int timeout_ms;
};

gboolean timerPrepare(GSource * source, gint * timeout) {
  int ms = reinterpret_cast<TimerSource *>(source)->timeout_ms;
  *timeout = ms;
  return ms == 0;
}
gboolean timerDispatch(GSource *, GSourceFunc, gpointer) { return G_SOURCE_CONTINUE; }
GSourceFuncs timer_funcs = { timerPrepare, nullptr, timerDispatch, nullptr, nullptr, nullptr };

}  // namespace

struct GlibEventLoop::Impl {
  TimerSource * timer = nullptr;
  std::vector<GSource *> sources;
  bool ran = false;
};

GlibEventLoop::GlibEventLoop() : impl_(new Impl) {
  impl_->timer = reinterpret_cast<TimerSource *>(g_source_new(&timer_funcs, sizeof(TimerSource)));
  impl_->timer->timeout_ms = -1;
  g_source_attach(&impl_->timer->base, nullptr);
}

GlibEventLoop::~GlibEventLoop() {
  for (GSource * source : impl_->sources) {
    g_source_destroy(source);
    g_source_unref(source);
  }
  g_source_destroy(&impl_->timer->base);
  g_source_unref(&impl_->timer->base);
}

void GlibEventLoop::addSource(const std::vector<int> & fds, Handler handler) {
  GSource * source = g_source_new(&fd_funcs, sizeof(FdSource));
  auto * s = reinterpret_cast<FdSource *>(source);
  s->tags = new std::vector<gpointer>;
  s->handler = new Handler(std::move(handler));
  s->ran = &impl_->ran;
  for (int fd : fds) s->tags->push_back(g_source_add_unix_fd(source, fd, G_IO_IN));
  g_source_attach(source, nullptr);
  impl_->sources.push_back(source);
}

bool GlibEventLoop::iterate(bool block) {
  int timeout = beforeTurn();
  impl_->timer->timeout_ms = block ? timeout : -1;
  impl_->ran = false;
  g_main_context_iteration(nullptr, block ? TRUE : FALSE);
  bool ran = impl_->ran;
  afterTurn(ran);
  return ran;
}

}  // namespace cellui::gtk
