#pragma once

#include <cstdint>

#include <map>
#include <set>

#include "wx/stc/stc.h"

namespace dsp56kDebugger
{
	class StyledTextCtrl : public wxStyledTextCtrl
	{
	public:
		StyledTextCtrl(wxWindow* _parent, wxStandardID _id, const wxPoint& _pos, const wxSize& _size);

		bool isFocused() const;

		// this one had the focus last of all StyledTextCtrls, also while a dialog has it
		bool isLastFocused() const;

	protected:
		void setLineCount(uint32_t _count);
		void replaceLines(int _line, const std::vector<std::string>& _lines);
		void addMarker(int _line, int _marker);
		void removeMarker(int _line, int _marker);
		void toggleMarker(int _line, int _marker, bool _enable);
		void removeAllMarkers(int _line);
		void clear();

		bool addBookmark(int _line);
		bool removeBookmark(int _line);
		void toggleBookmark(int _line);
		bool gotoNextBookmark();
		bool hasBookmark(int line) const;
		void markerDefineBookmark(int _markerNumber);

		// the last focused one until a StyledTextCtrl had the focus
		void setAsDefaultFocus();

	private:
		void onSetFocus(wxFocusEvent&);
		void onLoseFocus(wxFocusEvent&);

		std::vector<std::string> m_lines;
		std::map<int, std::map<int, int>> m_markers;

		std::set<int> m_bookmarks;

		bool m_focused = false;

		static StyledTextCtrl* s_lastFocused;

		wxDECLARE_EVENT_TABLE();
	};
}
