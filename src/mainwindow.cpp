/**
 * Copyright (C) 2019 Francesco Fusco. All rights reserved.
 * License: https://github.com/Fushko/gammy#license
 */

#include "ui_mainwindow.h"
#include "main.h"
#include "utils.h"
#include "cfg.h"

#ifdef _WIN32
	#include <Windows.h>
#endif

#include <QScreen>
#include <QSystemTrayIcon>
#include <QToolTip>
#include <QHelpEvent>
#include <QAction>
#include <QMenu>
#include <QTime>

#include "mainwindow.h"
#include "tempscheduler.h"

#ifndef _WIN32

MainWindow::MainWindow(X11 *x11, convar *auto_cv, convar *temp_cv)
	: ui(new Ui::MainWindow), trayIcon(new QSystemTrayIcon(this))
{
	this->auto_cv = auto_cv;
	this->temp_cv = temp_cv;

	this->x11 = x11;

	init();
}
#endif

MainWindow::MainWindow(QWidget *parent, convar *auto_cv, convar *temp_cv)
	: QMainWindow(parent), ui(new Ui::MainWindow), trayIcon(new QSystemTrayIcon(this))
{
	this->auto_cv = auto_cv;
	this->temp_cv = temp_cv;

	init();
}

void MainWindow::init()
{
	ui->setupUi(this);

	QIcon icon = QIcon(":res/icons/128x128ball.ico");

	// Set window properties
	{
		this->setWindowTitle("Gammy");
		this->setWindowIcon(icon);
		resize(335, 333);

		QApplication::setAttribute(Qt::AA_DisableWindowContextHelpButton);

		this->setWindowFlags(Qt::Dialog | Qt::WindowStaysOnTopHint);

		// Deprecated buttons, to be removed altogether
		ui->closeButton->hide();
		ui->hideButton->hide();

		if constexpr(os == OS::Windows)
		{
			// Extending brightness range doesn't work yet on Windows
			ui->extendBr->hide();
		}

		ui->manBrSlider->hide();

		// Move window to bottom right
		QRect scr = QGuiApplication::primaryScreen()->availableGeometry();
		move(scr.width() - this->width(), scr.height() - this->height());
	}

	// Create tray icon
	{
		if (!QSystemTrayIcon::isSystemTrayAvailable())
		{
			LOGW << "System tray unavailable. Closing the settings window will quit the app";

			ignore_closeEvent = false;
			show();
		}

		this->trayIcon->setIcon(icon);

		QMenu *menu = createMenu();
		this->trayIcon->setContextMenu(menu);
		this->trayIcon->setToolTip(QString("Gammy"));
		this->trayIcon->show();
		connect(trayIcon, &QSystemTrayIcon::activated, this, &MainWindow::iconActivated);

		LOGI << "Tray icon created";
	}

	// Set slider properties
	{
		ui->extendBr->setChecked(cfg["extend_br"]);
		setBrSlidersRange(cfg["extend_br"]);

		ui->tempSlider->setRange(0, temp_arr_entries * temp_mult);
		ui->minBrSlider->setValue(cfg["min_br"]);
		ui->maxBrSlider->setValue(cfg["max_br"]);
		ui->offsetSlider->setValue(cfg["offset"]);
		ui->speedSlider->setValue(cfg["speed"]);
		ui->tempSlider->setValue(cfg["temp_step"]);
		ui->thresholdSlider->setValue(cfg["threshold"]);
		ui->pollingSlider->setValue(cfg["polling_rate"]);
	}

	// Set auto brightness/temp toggles
	{
		ui->autoCheck->setChecked(cfg["auto_br"]);

		run_ss_thread = cfg["auto_br"];
		auto_cv->notify_one();

		toggleSliders(cfg["auto_br"]);

		ui->autoTempCheck->setChecked(cfg["auto_temp"]);
	}

	LOGI << "Window initialized";
}

QMenu* MainWindow::createMenu()
{
	QMenu *menu = new QMenu(this);

#ifdef _WIN32
	QAction *run_startup = new QAction("&Run at startup", this);
	run_startup->setCheckable(true);

	connect(run_startup, &QAction::triggered, [=]{ toggleRegkey(run_startup->isChecked()); });
	menu->addAction(run_startup);

	LRESULT s = RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"Gammy", RRF_RT_REG_SZ, nullptr, nullptr, nullptr);

	s == ERROR_SUCCESS ? run_startup->setChecked(true): run_startup->setChecked(false);
#else

	QAction *show_wnd = new QAction("&Show Gammy", this);

	const auto show_on_top = [this]
	{
		if(!this->isHidden()) return;

		setWindowFlags(Qt::Dialog | Qt::WindowStaysOnTopHint);

		// Move the window to bottom right again.
		// For some reason it moves up otherwise.
		QRect scr = QGuiApplication::primaryScreen()->availableGeometry();
		move(scr.width() - this->width(), scr.height() - this->height());

		show();
		updateBrLabel();
	};

	connect(show_wnd, &QAction::triggered, this, show_on_top);
	menu->addAction(show_wnd);
#endif

	menu->addSeparator();

	QAction *quit_prev = new QAction("&Quit", this);
	connect(quit_prev, &QAction::triggered, this, [&]() { on_closeButton_clicked(true); });
	menu->addAction(quit_prev);

#ifndef _WIN32
	QAction *quit_pure = new QAction("&Quit (set pure gamma)", this);
	connect(quit_pure, &QAction::triggered, this, [&]() { on_closeButton_clicked(false); });
	menu->addAction(quit_pure);
#endif

	return menu;
}

void MainWindow::iconActivated(QSystemTrayIcon::ActivationReason reason)
{
	if (reason == QSystemTrayIcon::Trigger)
	{
		MainWindow::updateBrLabel();
		MainWindow::show();
	}
}

void MainWindow::updateBrLabel()
{
	if(isVisible())
	{
		int val = scr_br * 100 / 255;
		ui->statusLabel->setText(QStringLiteral("%1").arg(val));
	}
}

void MainWindow::on_hideButton_clicked()
{
	this->hide();
}

//___________________________________________________________

void MainWindow::on_minBrSlider_valueChanged(int val)
{
	ui->minBrLabel->setText(QStringLiteral("%1").arg(val * 100 / 255));

	if(val > cfg["max_br"])
	{
		ui->maxBrSlider->setValue(cfg["max_br"] = val);
	}

	cfg["min_br"] = val;
}

void MainWindow::on_maxBrSlider_valueChanged(int val)
{
	ui->maxBrLabel->setText(QStringLiteral("%1").arg(val * 100 / 255));

	if(val < cfg["min_br"])
	{
		ui->minBrSlider->setValue(cfg["min_br"] = val);
	}

	cfg["max_br"] = val;
}

void MainWindow::on_offsetSlider_valueChanged(int val)
{
	cfg["offset"] = val;

	ui->offsetLabel->setText(QStringLiteral("%1").arg(val * 100 / 255));
}

void MainWindow::on_speedSlider_valueChanged(int val)
{
	cfg["speed"] = val;
}

void MainWindow::on_tempSlider_valueChanged(int val)
{
	cfg["temp_step"] = val;

	if(this->quit) return;

	if constexpr(os == OS::Windows) {
		setGDIGamma(scr_br, val);
	}
#ifndef _WIN32
	else x11->setXF86Gamma(scr_br, val);
#endif

	int temp_kelvin = convertRange(temp_arr_entries * temp_mult - val, 0, temp_arr_entries * temp_mult, min_temp_kelvin, max_temp_kelvin);

	temp_kelvin = ((temp_kelvin - 1) / 100 + 1) * 100;

	ui->tempLabel->setText(QStringLiteral("%1").arg(temp_kelvin));
}

void MainWindow::on_thresholdSlider_valueChanged(int val)
{
	cfg["threshold"] = val;
}

void MainWindow::on_pollingSlider_valueChanged(int val)
{
	cfg["polling_rate"] = val;
}

void MainWindow::on_autoCheck_toggled(bool checked)
{
	run_ss_thread		= checked;
	if(force) *force	= checked;
	auto_cv->notify_all();

	toggleSliders(checked);
	cfg["auto_br"]		= checked;
}

void MainWindow::on_autoTempCheck_toggled(bool checked)
{
	cfg["auto_temp"]	= checked;
	run_temp_thread		= checked;

	if(force_temp_change) *force_temp_change = checked;

	ui->tempSlider->setDisabled(checked);

	temp_cv->notify_all();
}

void MainWindow::toggleSliders(bool is_auto)
{
	if(is_auto)
	{
		ui->manBrSlider->hide();
	}
	else
	{
		ui->manBrSlider->setValue(scr_br);
		ui->manBrSlider->show();
	}
}

void MainWindow::on_manBrSlider_valueChanged(int value)
{
	scr_br = value;
	cfg["brightness"] = value;

	if constexpr(os == OS::Windows) {
		setGDIGamma(scr_br, cfg["temp_step"]);
	}
#ifndef _WIN32
	else x11->setXF86Gamma(scr_br, cfg["temp_step"]);
#endif

	updateBrLabel();
}

void MainWindow::on_extendBr_clicked(bool checked)
{
	cfg["extend_br"] = checked;

	setBrSlidersRange(cfg["extend_br"]);
}

void MainWindow::setBrSlidersRange(bool inc)
{
	LOGV << "Setting sliders range";

	int br_limit = default_brightness;

	if(inc) br_limit *= 2;

	ui->manBrSlider->setRange(64, br_limit);
	ui->minBrSlider->setRange(64, br_limit);
	ui->maxBrSlider->setRange(64, br_limit);
	ui->offsetSlider->setRange(0, br_limit);
}

void MainWindow::on_pushButton_clicked()
{
	TempScheduler ts(nullptr, temp_cv, force_temp_change);
	ts.exec();
}

void MainWindow::setPollingRange(int min, int max)
{
	const int poll = cfg["polling_rate"];

	LOGD << "Setting polling rate slider range to: " << min << ", " << max;

	ui->pollingSlider->setRange(min, max);

	if(poll < min) {
		cfg["polling_rate"] = min;
	}
	else
	if(poll > max) {
		cfg["polling_rate"] = max;
	}

	ui->pollingLabel->setText(QString::number(poll));
	ui->pollingSlider->setValue(poll);
}

void MainWindow::setTempSlider(int val)
{
	ui->tempSlider->setValue(val);
}

void MainWindow::on_tempSlider_sliderPressed()
{
	// @TODO: Something
}

void MainWindow::on_closeButton_clicked(bool set_previous_gamma)
{
	// Boolean read by screenshot thread before quitting
	this->set_previous_gamma = set_previous_gamma;

	quit = true;
	auto_cv->notify_all();
	temp_cv->notify_one();

	QCloseEvent e;
	e.setAccepted(true);
	emit closeEvent(&e);

	trayIcon->hide();
}

void MainWindow::closeEvent(QCloseEvent *e)
{
	this->hide();

	save();

	if(ignore_closeEvent) e->ignore();
}

MainWindow::~MainWindow()
{
	delete ui;
}
