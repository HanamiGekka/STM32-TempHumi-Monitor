#include "app_plot_window.h"
#include <cmath>
#include <algorithm>
bool app_plot_window_t::app_set_mode(bool manual,int seconds)
{
    if(seconds<5 ||seconds>1200) return false;
    m_manual=manual; if(manual) m_manual_span=seconds; else m_auto_span=5;
    return true;
}
void app_plot_window_t::app_reset_segment() { m_auto_span=5; }
app_plot_range_t app_plot_window_t::app_range_at(double elapsed_s)
{
    if(!std::isfinite(elapsed_s) ||elapsed_s<0) elapsed_s=0;
    if(!m_manual) {
        static const int steps[]={5,10,20,30,60,120,300,600,1200};
        for(int i=0;i<8;++i)
            if(m_auto_span==steps[i] && elapsed_s>=steps[i]*2.0/3.0) m_auto_span=steps[i+1];
    }
    app_plot_range_t r; r.span=app_span();
    // AUTO steps extend before scrolling. At maximum AUTO or fixed MANUAL,
    // the latest point reaches 90% then leaves a 10% right-hand margin.
    r.lower=(m_manual ||r.span==1200)?std::max(0.0,elapsed_s-r.span*.9):0.0;
    r.upper=r.lower+r.span; return r;
}
