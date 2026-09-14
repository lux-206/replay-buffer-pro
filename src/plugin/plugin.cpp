/**
 * @file plugin.cpp
 * @brief Implementation of the Replay Buffer Pro plugin
 *
 * Provides enhanced replay buffer controls for OBS Studio including:
 * - Configurable buffer length adjustment
 * - Segment-based replay saving
 * - Automatic replay trimming
 */

// OBS includes
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <util/config-file.h>
#include <util/platform.h>

// Qt includes
#include <QMessageBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>

// STL includes
#include <string>
#include <vector>

// Local includes
#include "utils/obs-utils.hpp"
#include "plugin/plugin.hpp"
#include "config/config.hpp"
#include "utils/logger.hpp"

namespace ReplayBufferPro
{
	//=============================================================================
	// CONSTRUCTORS & DESTRUCTOR
	//=============================================================================

	Plugin::Plugin(QWidget *parent)
			: QWidget(parent), 
			  lastKnownBufferLength(0)
	{
		// Create component instances
		cloudUploadManager = new Cloud::CloudUploadManager();
		replayManager = new ReplayBufferManager(this);
		replayManager->setClipReadyCallback([this](const std::string &path) {
			if (cloudUploadManager) cloudUploadManager->enqueueClip(path);
		});
		settingsManager = new SettingsManager();
		saveButtonSettings = new SaveButtonSettings();
		saveButtonSettings->load();
		
		// Create UI components with callbacks
		ui = new UIComponents(this, 
			[this](int duration) { handleSaveSegment(duration); },
			[this]() { handleSaveFullBuffer(); },
			[this]() { handleCustomizeSaveButtons(); },
			[this]() { handleCloudSettings(); }
		);
		ui->setSaveButtonDurations(saveButtonSettings->getDurations());
		
		// Mount the UI into this widget
		{
			QWidget *root = ui->createUI();
			auto *layout = new QVBoxLayout(this);
			layout->setContentsMargins(0, 0, 0, 0);
			layout->setSpacing(0);
			layout->addWidget(root);
			setLayout(layout);
		}
		
		// Initialize signals and load settings
		initSignals();
		loadBufferLength();

		// Register OBS event callback
		obs_frontend_add_event_callback(handleOBSEvent, this);

		// Create and register hotkeys
		hotkeyManager = new HotkeyManager(
			[this](int duration) { handleSaveSegment(duration); },
			saveButtonSettings->getDurations()
		);
		hotkeyManager->registerHotkeys();

		// Setup settings monitoring
		settingsMonitorTimer = new QTimer(this);
		settingsMonitorTimer->setInterval(Config::SETTINGS_MONITOR_INTERVAL);
		connect(settingsMonitorTimer, &QTimer::timeout, this, &Plugin::loadBufferLength);
		settingsMonitorTimer->start();
		ui->updateCloudStatus(cloudUploadManager->isConnected(), cloudUploadManager->pendingCount());
	}

	// Removed QMainWindow-based constructor; OBS wraps QWidget into a dock

	Plugin::~Plugin()
	{
		// Stop the settings monitor timer first
		if (settingsMonitorTimer) {
			settingsMonitorTimer->stop();
		}
		
		// Remove OBS callbacks before destroying components
		obs_frontend_remove_event_callback(handleOBSEvent, this);
		replayManager->setClipReadyCallback({});
		delete replayManager;
		replayManager = nullptr;
		delete cloudUploadManager;
		cloudUploadManager = nullptr;
		
		// Clean up managers that were allocated with new
		delete hotkeyManager;
		delete saveButtonSettings;
		delete settingsManager;
		
		// Qt parent-child relationship will handle cleanup for other components
	}

	//=============================================================================
	// INITIALIZATION
	//=============================================================================

	void Plugin::initSignals()
	{
		connect(ui->getSecondsEdit(), QOverload<int>::of(&QSpinBox::valueChanged),
				this, &Plugin::handleBufferLengthChanged);

		connect(ui->getBufferLengthDebounceTimer(), &QTimer::timeout, this, &Plugin::handleBufferLengthFinished);
	}

	//=============================================================================
	// EVENT HANDLERS
	//=============================================================================

	void Plugin::handleOBSEvent(enum obs_frontend_event event, void *ptr)
	{
		auto plugin = static_cast<Plugin *>(ptr);

		switch (event)
		{
		case OBS_FRONTEND_EVENT_EXIT:
			if (plugin->settingsMonitorTimer) {
				plugin->settingsMonitorTimer->stop();
			}
			if (plugin->hotkeyManager) {
				plugin->hotkeyManager->saveHotkeySettings();
			}
			break;
		case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STARTING:
			plugin->settingsMonitorTimer->stop();
			QMetaObject::invokeMethod(plugin, "updateBufferLengthUIState", Qt::QueuedConnection);
			break;
		case OBS_FRONTEND_EVENT_REPLAY_BUFFER_STOPPED:
			plugin->settingsMonitorTimer->start();
			QMetaObject::invokeMethod(plugin, "updateBufferLengthUIState", Qt::QueuedConnection);
			QMetaObject::invokeMethod(plugin, "loadBufferLength", Qt::QueuedConnection);
			break;
		case OBS_FRONTEND_EVENT_REPLAY_BUFFER_SAVED:
			plugin->handleReplayBufferSaved();
			break;
		default:
			break;
		}
	}

	void Plugin::handleBufferLengthChanged(int value)
	{
		// Only update the UI components, don't trigger settings update yet
		ui->updateBufferLengthValue(value);

		// Restart the debounce timer
		ui->getBufferLengthDebounceTimer()->start();
	}

	void Plugin::handleBufferLengthFinished()
	{
		// Get the final spinbox value
		int value = ui->getSecondsEdit()->value();

		// Only update settings if the value has actually changed
		if (value != lastKnownBufferLength)
		{
			try {
				settingsManager->updateBufferLengthSettings(value);
				lastKnownBufferLength = value;
			} catch (const std::exception &e) {
				QMessageBox::warning(this, obs_module_text("Error"),
									QString(obs_module_text("FailedToUpdateLength")).arg(e.what()));
			}
		}
	}

	void Plugin::handleSaveFullBuffer()
	{
		replayManager->saveFullBuffer(this);
	}

	void Plugin::handleSaveSegment(int duration)
	{
		replayManager->saveSegment(duration, this);
	}

	void Plugin::handleCloudSettings()
	{
		Cloud::CloudSettings settings = cloudUploadManager->settings();
		QDialog dialog(this);
		dialog.setWindowTitle(obs_module_text("CloudSettingsTitle"));
		auto *layout = new QVBoxLayout(&dialog);
		auto *form = new QFormLayout();
		auto *enabled = new QCheckBox(&dialog);
		enabled->setChecked(settings.enabled);
		auto *provider = new QComboBox(&dialog);
		provider->addItem("Google Drive");
		provider->setEnabled(false);
		auto *account = new QLabel(cloudUploadManager->isConnected()
			? obs_module_text("CloudConnected") : obs_module_text("CloudNotConnected"), &dialog);
		auto *destination = new QLineEdit(QString::fromStdString(settings.destination), &dialog);
		auto *deleteLocal = new QCheckBox(&dialog);
		deleteLocal->setChecked(settings.deleteLocalAfterUpload);
		auto *retry = new QCheckBox(&dialog);
		retry->setChecked(settings.retryFailedUploads);
		auto *concurrency = new QSpinBox(&dialog);
		concurrency->setRange(1, 1);
		concurrency->setValue(1);
		auto *clientId = new QLineEdit(&dialog);
		auto *clientSecret = new QLineEdit(&dialog);
		clientSecret->setEchoMode(QLineEdit::Password);
		clientSecret->setPlaceholderText("Stored encrypted with Windows DPAPI");
		form->addRow(obs_module_text("CloudEnable"), enabled);
		form->addRow(obs_module_text("CloudProvider"), provider);
		form->addRow(obs_module_text("CloudClientId"), clientId);
		form->addRow(obs_module_text("CloudClientSecret"), clientSecret);
		form->addRow(obs_module_text("CloudDestination"), destination);
		form->addRow(obs_module_text("CloudDeleteLocal"), deleteLocal);
		form->addRow(obs_module_text("CloudRetry"), retry);
		form->addRow(obs_module_text("CloudConcurrent"), concurrency);
		form->addRow(obs_module_text("CloudPending"), new QLabel(QString::number(cloudUploadManager->pendingCount()), &dialog));
		form->addRow("Google account", account);
		layout->addLayout(form);

		auto *accountButtons = new QHBoxLayout();
		auto *connectButton = new QPushButton(obs_module_text("CloudConnect"), &dialog);
		auto *disconnectButton = new QPushButton(obs_module_text("CloudDisconnect"), &dialog);
		accountButtons->addWidget(connectButton);
		accountButtons->addWidget(disconnectButton);
		layout->addLayout(accountButtons);
		connect(connectButton, &QPushButton::clicked, &dialog, [&, this] {
			std::string error;
			if (cloudUploadManager->connectGoogle(clientId->text().toStdString(),
				clientSecret->text().toStdString(), &dialog, &error)) {
				account->setText(obs_module_text("CloudConnected"));
				clientSecret->clear();
			} else {
				QMessageBox::warning(&dialog, obs_module_text("Error"), QString::fromStdString(error));
			}
		});
		connect(disconnectButton, &QPushButton::clicked, &dialog, [&, this] {
			std::string error;
			if (cloudUploadManager->disconnectGoogle(&error)) account->setText(obs_module_text("CloudNotConnected"));
			else QMessageBox::warning(&dialog, obs_module_text("Error"), QString::fromStdString(error));
		});

		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
		connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
		connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
		layout->addWidget(buttons);
		if (dialog.exec() == QDialog::Accepted) {
			settings.enabled = enabled->isChecked();
			settings.destination = destination->text().trimmed().toStdString();
			settings.deleteLocalAfterUpload = deleteLocal->isChecked();
			settings.retryFailedUploads = retry->isChecked();
			std::string error;
			if (!cloudUploadManager->updateSettings(settings, &error))
				QMessageBox::warning(this, obs_module_text("Error"), QString::fromStdString(error));
		}
		ui->updateCloudStatus(cloudUploadManager->isConnected(), cloudUploadManager->pendingCount());
	}

	void Plugin::handleReplayBufferSaved()
	{
		std::string savedPath;

		if (const char* path = obs_frontend_get_last_replay()) {
			savedPath = path;
			bfree((void*)path);
		}

		// The manager matches this to the request that produced it, logs a verdict
		// either way, and hands any trimming off to its own worker thread so the
		// OBS event thread is never blocked.
		replayManager->handleSaveCompleted(savedPath);
	}

	void Plugin::handleCustomizeSaveButtons()
	{
		QDialog dialog(this);
		dialog.setWindowTitle(obs_module_text("CustomizeButtonsTitle"));
		QVBoxLayout *layout = new QVBoxLayout(&dialog);

		QFormLayout *formLayout = new QFormLayout();
		std::vector<QSpinBox *> inputs;
		inputs.reserve(Config::SAVE_BUTTON_COUNT);

		const auto &durations = saveButtonSettings->getDurations();
		for (size_t i = 0; i < Config::SAVE_BUTTON_COUNT; i++)
		{
			QSpinBox *spinBox = new QSpinBox(&dialog);
			spinBox->setRange(1, Config::MAX_BUFFER_LENGTH);
			spinBox->setSuffix(" sec");
			if (i < durations.size())
			{
				spinBox->setValue(durations[i]);
			}

			QString labelText = QString::fromUtf8(obs_module_text("SaveClipButtonLabel")).arg(i + 1);
			formLayout->addRow(labelText, spinBox);
			inputs.push_back(spinBox);
		}

		layout->addLayout(formLayout);

		QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
		buttonBox->button(QDialogButtonBox::Ok)->setText(obs_module_text("CustomizeButtonsSave"));
		buttonBox->button(QDialogButtonBox::Cancel)->setText(obs_module_text("CustomizeButtonsCancel"));
		connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
		connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
		layout->addWidget(buttonBox);

		if (dialog.exec() != QDialog::Accepted)
		{
			return;
		}

		std::vector<int> updatedDurations;
		updatedDurations.reserve(inputs.size());
		for (auto *input : inputs)
		{
			updatedDurations.push_back(input->value());
		}

		saveButtonSettings->setDurations(updatedDurations);
		if (!saveButtonSettings->save())
		{
			Logger::warning("Failed to save custom save button durations");
		}

		ui->setSaveButtonDurations(saveButtonSettings->getDurations());
		if (hotkeyManager)
		{
			hotkeyManager->setSaveButtonDurations(saveButtonSettings->getDurations());
		}
	}

	//=============================================================================
	// UI STATE MANAGEMENT
	//=============================================================================

	void Plugin::updateBufferLengthUIState()
	{
		bool isActive = obs_frontend_replay_buffer_active();
		ui->updateBufferLengthState(isActive);
	}

	//=============================================================================
	// SETTINGS MANAGEMENT
	//=============================================================================

	void Plugin::loadBufferLength()
	{
		int bufferLength = settingsManager->getCurrentBufferLength();
		if (bufferLength > 0 && bufferLength != lastKnownBufferLength)
		{
			lastKnownBufferLength = bufferLength;
			ui->updateBufferLengthValue(bufferLength);
		}
		if (ui && cloudUploadManager)
			ui->updateCloudStatus(cloudUploadManager->isConnected(), cloudUploadManager->pendingCount());
	}

} // namespace ReplayBufferPro
