#include "mediator.h"
#include "mainwindow.h"
#include "gammactl.h"
#include "cfg.h"

Mediator::Mediator(GammaCtl *g, MainWindow *w) : gammactl(g), wnd(w)
{
	gammactl->set_mediator(this);
	wnd->set_mediator(this);

	gammactl->start();
	wnd->init();
}

void Mediator::notify(Component *sender, Component::Event e) const
{
	switch (e) {
	case Component::BRT_CHANGED:
		wnd->setBrtSlider(cfg["brightness"]);
		break;
	case Component::TEMP_CHANGED:
		wnd->setTempSlider(cfg["temp_step"]);
		break;
	case Component::GAMMA_SLIDER_MOVED:
		gammactl->setGamma(cfg["brightness"], cfg["temp_step"]);
		break;
	case Component::AUTO_BRT_TOGGLED:
		LOGD << "Auto brt toggled.";
		gammactl->notify_ss();
		break;
	case Component::AUTO_TEMP_TOGGLED:
		LOGD << "Auto temp toggled.";
		gammactl->notify_temp(true);
		break;
	case Component::SYSTEM_WAKE_UP:
		LOGD << "System woke up from sleep.";
		gammactl->notify_temp(true);
		break;
	case Component::APP_QUIT:
		gammactl->stop();
		gammactl->setInitialGamma(true);
		break;
	case Component::APP_QUIT_PURE_GAMMA:
		gammactl->stop();
		gammactl->setInitialGamma(false);
		break;
	}
}