/*
 *  Licensed to the Apache Software Foundation (ASF) under one or more
 *  contributor license agreements.  See the NOTICE file distributed with
 *  this work for additional information regarding copyright ownership.
 *  The ASF licenses this file to You under the Apache License, Version 2.0
 *  (the "License"); you may not use this file except in compliance with
 *  the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

package org.apache.commons.daemon;


/**
 *
 * @author Pier Fumagalli
 * @version 1.0 <i>(CVS $Revision: 480475 $)</i>
 */
public interface DaemonController {

    /**
     *
     */
    public void shutdown()
    throws IllegalStateException;

    /**
     *
     */
    public void reload()
    throws IllegalStateException;

    /**
     *
     */
    public void fail()
    throws IllegalStateException;

    /**
     *
     */
    public void fail(String message)
    throws IllegalStateException;

    /**
     *
     */
    public void fail(Exception exception)
    throws IllegalStateException;

    /**
     *
     */
    public void fail(String message, Exception exception)
    throws IllegalStateException;

}
