#pragma once

#include "RobloxModLoader/qt/qwidget.hpp"

#include <functional>
#include <string>
#include <string_view>

namespace rml::qt
{
	class QIcon;

	class RML_EXPORT QAbstractButton : public QWidget
	{
	public:
		void setText(std::string_view text);
		[[nodiscard]] std::string text() const;
		void setIcon(const QIcon& icon);
		void setIconSize(int width, int height);

		void setChecked(bool checked);
		[[nodiscard]] bool isChecked() const;

		void on_clicked(std::function<void()> handler) const;
		void on_toggled(std::function<void(bool)> handler) const;
	};
}
