import React from 'react';
import { Link } from "react-router-dom";
import menulogo from '../menu.svg'
import '../App.css';


class Param extends React.Component {
    

    constructor(props) {

        super(props);
        
        this.state = {
            toto:"value"
        };
        
    }
    
    componentDidMount ()
    {
      
      
      
    }
    
    render ()
    {
        
            return (
                <>
                <h1>BrailleRAP</h1>
                <nav>
                <Link to="/">
                <img src={menulogo} className="menu" alt="menu" />
                </Link>
                </nav>
           
                </>
            );
    }
}

export default Param;