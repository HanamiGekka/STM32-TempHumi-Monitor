#ifndef APP_PLOT_WINDOW_H
#define APP_PLOT_WINDOW_H
// Source-segment seconds only. No wall-clock or measurement transformation.
struct app_plot_range_t { double lower=0,upper=5; int span=5; };
class app_plot_window_t {
public:
    bool app_set_mode(bool manual,int seconds);
    void app_reset_segment();
    app_plot_range_t app_range_at(double elapsed_s);
    bool app_manual() const { return m_manual; }
    int app_span() const { return m_manual?m_manual_span:m_auto_span; }
private:
    bool m_manual=false;
    int m_manual_span=32,m_auto_span=5;
};
#endif
