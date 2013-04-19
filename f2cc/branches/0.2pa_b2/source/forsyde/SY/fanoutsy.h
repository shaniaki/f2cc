/*
 * Copyright (c) 2011-2013 Gabriel Hjort Blindell <ghb@kth.se>
 *                          George Ungureanu <ugeorge@kth.se>
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright notice,
 *       this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS OF THIS SOFTWARE NOR THE
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef F2CC_SOURCE_FORSYDE_COPY_H_
#define F2CC_SOURCE_FORSYDE_COPY_H_

/**
 * @file
 * @author  Gabriel Hjort Blindell <ghb@kth.se>
 * @version 0.1
 *
 * @brief Implements the a process for copying a signal value to multiple output
 *        signals.
 */

#include "../process.h"
#include "../../exceptions/notsupportedexception.h"
#include <string>

namespace f2cc {
namespace ForSyDe {
namespace SY {

/**
 * @brief Implements the a process for copying a signal value to multiple output
 *        signals.
 *
 * The \c fanout process is a special process whose only purpose is to copy the
 * value on the input signal to all of its output signals. The internal process network
 * does no allow a port to be connected to multiple other ports. However,
 * ForSyDe itself does allow multiple signals to retrieve its values from the
 * same source. Thus, during parsing when encountering such instances, an
 * intermediate \c fanout process is created and the signals redirected to its
 * outputs.
 */
class fanout : public Process {
  public:
    /**
     * @copydoc Process(const Id&)
     */
    fanout(const Id& id, const std::string& moc) throw();

    /**
     * @copydoc ~Process()
     */
    virtual ~fanout() throw();

    /**
     * @copydoc Process::operator==(const Process&) const
     */
    virtual bool operator==(const Process& rhs) const throw();

    /**
     * @copydoc Process::type()
     */
    virtual std::string type() const throw();

  protected:
    /**
     * Checks that this process has only one in port.
     *
     * @throws InvalidProcessException
     *         When the check fails.
     */
    virtual void moreChecks() throw(InvalidProcessException);
};

}
}
}

#endif
