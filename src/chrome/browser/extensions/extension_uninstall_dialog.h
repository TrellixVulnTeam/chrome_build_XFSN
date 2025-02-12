// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_EXTENSIONS_EXTENSION_UNINSTALL_DIALOG_H_
#define CHROME_BROWSER_EXTENSIONS_EXTENSION_UNINSTALL_DIALOG_H_

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observer.h"
#include "base/strings/string16.h"
#include "base/threading/thread_checker.h"
#include "chrome/browser/extensions/chrome_app_icon.h"
#include "chrome/browser/extensions/chrome_app_icon_delegate.h"
#include "extensions/browser/extension_registry_observer.h"
#include "extensions/browser/uninstall_reason.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/native_widget_types.h"

class NativeWindowTracker;
class Profile;

namespace extensions {
class Extension;

class ExtensionUninstallDialog
    : public base::SupportsWeakPtr<ExtensionUninstallDialog>,
      public ChromeAppIconDelegate,
      public ExtensionRegistryObserver {
 public:
  // The type of action the dialog took at close.
  // Do not reorder this enum, as it is used in UMA histograms.
  enum CloseAction {
    CLOSE_ACTION_UNINSTALL = 0,
    CLOSE_ACTION_UNINSTALL_AND_REPORT_ABUSE = 1,
    CLOSE_ACTION_CANCELED = 2,
    CLOSE_ACTION_LAST = 3,
  };

  // TODO(devlin): For a single method like this, a callback is probably more
  // appropriate than a delegate.
  class Delegate {
   public:
    // Called when the dialog closes.
    // |did_start_uninstall| indicates whether the uninstall process for the
    // extension started. If this is false, |error| will contain the reason.
    virtual void OnExtensionUninstallDialogClosed(
        bool did_start_uninstall,
        const base::string16& error) = 0;

   protected:
    virtual ~Delegate() {}
  };

  // Creates a platform specific implementation of ExtensionUninstallDialog. The
  // dialog will be modal to |parent|, or a non-modal dialog if |parent| is
  // NULL.
  static ExtensionUninstallDialog* Create(Profile* profile,
                                          gfx::NativeWindow parent,
                                          Delegate* delegate);

  ~ExtensionUninstallDialog() override;

  // This is called to verify whether the uninstallation should proceed.
  // Starts the process of showing a confirmation UI, which is split into two.
  // 1) Set off a 'load icon' task.
  // 2) Handle the load icon response and show the UI (OnImageLoaded).
  void ConfirmUninstall(const scoped_refptr<const Extension>& extension,
                        UninstallReason reason,
                        UninstallSource source);

  // This shows the same dialog as above, except it also shows which extension
  // triggered the dialog.
  void ConfirmUninstallByExtension(
      const scoped_refptr<const Extension>& extension,
      const scoped_refptr<const Extension>& triggering_extension,
      UninstallReason reason,
      UninstallSource source);

  std::string GetHeadingText();

  // Returns true if a checkbox for reporting abuse should be shown.
  bool ShouldShowReportAbuseCheckbox() const;

  // Called when the dialog is closing to do any book-keeping.
  void OnDialogClosed(CloseAction action);

 protected:
  // Constructor used by the derived classes.
  ExtensionUninstallDialog(Profile* profile,
                           gfx::NativeWindow parent,
                           Delegate* delegate);

  // Accessors for members.
  const Profile* profile() const { return profile_; }
  Delegate* delegate() const { return delegate_; }
  const Extension* extension() const { return extension_.get(); }
  const Extension* triggering_extension() const {
      return triggering_extension_.get(); }
  const gfx::ImageSkia& icon() const { return icon_->image_skia(); }
  gfx::NativeWindow parent() { return parent_; }

 private:
  // Uninstalls the extension. Returns true on success, and populates |error| on
  // failure.
  bool Uninstall(base::string16* error);

  // Handles the "report abuse" checkbox being checked at the close of the
  // dialog.
  void HandleReportAbuse();

  // ChromeAppIconDelegate:
  void OnIconUpdated(ChromeAppIcon* icon) override;

  // ExtensionRegistryObserver:
  void OnExtensionUninstalled(content::BrowserContext* browser_context,
                              const Extension* extension,
                              UninstallReason reason) override;

  // Displays the prompt. This should only be called after loading the icon.
  // The implementations of this method are platform-specific.
  virtual void Show() = 0;

  Profile* const profile_;

  // The dialog's parent window.
  gfx::NativeWindow parent_;

  // The delegate we will call Accepted/Canceled on after confirmation dialog.
  Delegate* delegate_;

  // The extension we are showing the dialog for.
  scoped_refptr<const Extension> extension_;

  // The extension triggering the dialog if the dialog was shown by
  // chrome.management.uninstall.
  scoped_refptr<const Extension> triggering_extension_;

  std::unique_ptr<ChromeAppIcon> icon_;

  // Tracks whether |parent_| got destroyed.
  std::unique_ptr<NativeWindowTracker> parent_window_tracker_;

  // Indicates that dialog was shown.
  bool dialog_shown_ = false;

  UninstallReason uninstall_reason_ = UNINSTALL_REASON_FOR_TESTING;

  ScopedObserver<ExtensionRegistry, ExtensionRegistryObserver> observer_;

  base::ThreadChecker thread_checker_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionUninstallDialog);
};

}  // namespace extensions

#endif  // CHROME_BROWSER_EXTENSIONS_EXTENSION_UNINSTALL_DIALOG_H_
