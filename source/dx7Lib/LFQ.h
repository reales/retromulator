/**
 *  VDX7 - Virtual DX7 synthesizer emulation
 *  Copyright (C) 2023  chiaccona@gmail.com
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
**/

#pragma once

#include <atomic>
#include <cstddef>

namespace dx7Emu {

template<typename Element, size_t Size>
class CircularFifo {
public:
	enum { Capacity = Size+1 };

	CircularFifo() : _tail(0), _head(0){}
	virtual ~CircularFifo() {}

	bool push(const Element& item);
	bool pop(Element& item);

	bool wasEmpty() const;
	bool wasFull() const;
	bool isLockFree() const;

private:
	size_t increment(size_t idx) const;

	std::atomic <size_t>  _tail;
	Element    _array[Capacity];
	std::atomic<size_t>   _head;
};

template<typename Element, size_t Size>
bool CircularFifo<Element, Size>::push(const Element& item) {
	const auto current_tail = _tail.load(std::memory_order_relaxed);
	const auto next_tail = increment(current_tail);
	if(next_tail != _head.load(std::memory_order_acquire)) {
		_array[current_tail] = item;
		_tail.store(next_tail, std::memory_order_release);
		return true;
	}
	return false;
}

template<typename Element, size_t Size>
bool CircularFifo<Element, Size>::pop(Element& item) {
	const auto current_head = _head.load(std::memory_order_relaxed);
	if(current_head == _tail.load(std::memory_order_acquire))
		return false;
	item = _array[current_head];
	_head.store(increment(current_head), std::memory_order_release);
	return true;
}

template<typename Element, size_t Size>
bool CircularFifo<Element, Size>::wasEmpty() const {
	return (_head.load() == _tail.load());
}

template<typename Element, size_t Size>
bool CircularFifo<Element, Size>::wasFull() const {
	const auto next_tail = increment(_tail.load());
	return (next_tail == _head.load());
}

template<typename Element, size_t Size>
bool CircularFifo<Element, Size>::isLockFree() const {
	return (_tail.is_lock_free() && _head.is_lock_free());
}

template<typename Element, size_t Size>
size_t CircularFifo<Element, Size>::increment(size_t idx) const {
	return (idx + 1) % Capacity;
}

} // namespace dx7Emu
